#include "Raster.h"

#include <algorithm>
#include <string>

#ifdef _WIN32

// Windows: WIC reads Illustrator's renders, stacks them and writes the file
// with its resolution and JPEG quality. It runs on its own thread in a
// multithreaded apartment, so it never stalls Illustrator's UI apartment.

#include <windows.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <thread>
#include <vector>

namespace {

std::wstring Wide(const std::string& s)
{
	if (s.empty()) return L"";
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
	std::wstring w((size_t) n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int) s.size(), &w[0], n);
	std::replace(w.begin(), w.end(), L'/', L'\\');   // StorageFile wants backslashes
	return w;
}

std::string Hr(const char* what, HRESULT hr)
{
	char buf[16];
	snprintf(buf, sizeof buf, "0x%08lX", (unsigned long) hr);
	return std::string(what) + " failed (" + buf + ")";
}

std::string Encode(IWICImagingFactory* wic, IWICBitmapSource* frame, const slippy::RasterJob& job);

// Illustrator's renders (one per layer), each scaled to the asked size if it
// differs, stacked bottom first on white or clear, premultiplied BGRA.
std::string Stack(const slippy::RasterJob& job)
{
	winrt::com_ptr<IWICImagingFactory> wic;
	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()));
	if (FAILED(hr)) return Hr("WIC", hr);
	const UINT w = (UINT) job.pixelsWide, h = (UINT) job.pixelsHigh, stride = w * 4;
	std::vector<BYTE> canvas((size_t) stride * h, (BYTE) (job.png && job.transparent ? 0 : 255)), layer(canvas.size());
	for (const std::string& file : job.images) {
		winrt::com_ptr<IWICBitmapDecoder> decoder;
		winrt::com_ptr<IWICBitmapFrameDecode> frame;
		if (FAILED(hr = wic->CreateDecoderFromFilename(Wide(file).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put())))
			return Hr("Reading Illustrator's render", hr);
		if (FAILED(hr = decoder->GetFrame(0, frame.put()))) return Hr("Reading Illustrator's render", hr);
		winrt::com_ptr<IWICBitmapSource> source = frame;
		UINT fw = 0, fh = 0;
		frame->GetSize(&fw, &fh);
		if (fw != w || fh != h) {
			winrt::com_ptr<IWICBitmapScaler> scaler;
			if (FAILED(hr = wic->CreateBitmapScaler(scaler.put()))) return Hr("CreateBitmapScaler", hr);
			if (FAILED(hr = scaler->Initialize(frame.get(), w, h, WICBitmapInterpolationModeHighQualityCubic))) return Hr("Scaling the render", hr);
			source = scaler;
		}
		winrt::com_ptr<IWICFormatConverter> premultiplied;
		if (FAILED(hr = wic->CreateFormatConverter(premultiplied.put()))) return Hr("CreateFormatConverter", hr);
		if (FAILED(hr = premultiplied->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
			return Hr("Converting the render", hr);
		if (FAILED(hr = premultiplied->CopyPixels(nullptr, stride, (UINT) layer.size(), layer.data()))) return Hr("Reading the render", hr);
		for (size_t i = 0; i < canvas.size(); i += 4) {   // source over
			unsigned keep = 255 - layer[i + 3];
			for (size_t c = 0; c < 4; c++) canvas[i + c] = (BYTE) (layer[i + c] + (canvas[i + c] * keep + 127) / 255);
		}
	}
	winrt::com_ptr<IWICBitmap> stacked;
	if (FAILED(hr = wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, stride, (UINT) canvas.size(), canvas.data(), stacked.put())))
		return Hr("CreateBitmapFromMemory", hr);
	return Encode(wic.get(), stacked.get(), job);
}

// Runs fn on its own thread in a multithreaded apartment (see above).
template <class Fn> std::string OnWorker(Fn fn, const char* failure)
{
	std::string error;
	std::thread worker([&] {
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
			error = fn();
		}
		catch (const winrt::hresult_error& e) {
			error = std::string(failure) + ": " + winrt::to_string(e.message());
		}
		catch (...) {
			error = failure;
		}
		winrt::uninit_apartment();
	});
	worker.join();
	return error;
}

} // namespace

namespace slippy {

std::string EncodeImage(const RasterJob& job) { return OnWorker([&] { return Stack(job); }, "Windows couldn't write the image"); }

} // namespace slippy

namespace {

std::string Encode(IWICImagingFactory* wic, IWICBitmapSource* frame, const slippy::RasterJob& job)
{
	HRESULT hr;
	WICPixelFormatGUID format = job.png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
	winrt::com_ptr<IWICFormatConverter> converted;
	if (FAILED(hr = wic->CreateFormatConverter(converted.put()))) return Hr("CreateFormatConverter", hr);
	if (FAILED(hr = converted->Initialize(frame, format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return Hr("Converting the render", hr);

	winrt::com_ptr<IWICStream> out;
	if (FAILED(hr = wic->CreateStream(out.put()))) return Hr("CreateStream", hr);
	if (FAILED(hr = out->InitializeFromFilename(Wide(job.out).c_str(), GENERIC_WRITE))) return "couldn't write " + job.out;
	winrt::com_ptr<IWICBitmapEncoder> encoder;
	if (FAILED(hr = wic->CreateEncoder(job.png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg, nullptr, encoder.put()))) return Hr("CreateEncoder", hr);
	if (FAILED(hr = encoder->Initialize(out.get(), WICBitmapEncoderNoCache))) return Hr("Starting the file", hr);
	winrt::com_ptr<IWICBitmapFrameEncode> target;
	winrt::com_ptr<IPropertyBag2> props;
	if (FAILED(hr = encoder->CreateNewFrame(target.put(), props.put()))) return Hr("CreateNewFrame", hr);
	if (!job.png && props) {
		PROPBAG2 option = {};
		option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
		VARIANT value;
		VariantInit(&value);
		value.vt = VT_R4;
		value.fltVal = std::max(1, std::min(100, job.quality)) / 100.0f;
		props->Write(1, &option, &value);
	}
	if (FAILED(hr = target->Initialize(props.get()))) return Hr("Starting the image", hr);
	target->SetSize((UINT) job.pixelsWide, (UINT) job.pixelsHigh);
	target->SetResolution(job.dpi, job.dpi);
	target->SetPixelFormat(&format);
	if (FAILED(hr = target->WriteSource(converted.get(), nullptr))) return Hr("Writing the image", hr);
	if (FAILED(hr = target->Commit()) || FAILED(hr = encoder->Commit())) return "couldn't write " + job.out;
	return "";
}

} // namespace

#else // macOS

// Core Graphics stacks Illustrator's renders at the asked size; ImageIO writes it.

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

namespace {

CGContextRef NewCanvas(const slippy::RasterJob& job)
{
	CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef ctx = CGBitmapContextCreate(nullptr, job.pixelsWide, job.pixelsHigh, 8, 0, rgb, (CGBitmapInfo) kCGImageAlphaPremultipliedLast);
	CGColorSpaceRelease(rgb);
	if (!ctx) return nullptr;
	if (!(job.png && job.transparent)) { CGContextSetRGBFillColor(ctx, 1, 1, 1, 1); CGContextFillRect(ctx, CGRectMake(0, 0, job.pixelsWide, job.pixelsHigh)); }
	CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
	return ctx;
}

std::string TooBig(const slippy::RasterJob& job)
{
	return "not enough memory for a " + std::to_string(job.pixelsWide) + " x " + std::to_string(job.pixelsHigh) + " image";
}

// Writes the canvas to job.out and releases it.
std::string Write(CGContextRef ctx, const slippy::RasterJob& job)
{
	CGImageRef image = CGBitmapContextCreateImage(ctx);
	CGContextRelease(ctx);
	NSURL* out = [NSURL fileURLWithPath:[NSString stringWithUTF8String:job.out.c_str()]];
	CGImageDestinationRef dst = CGImageDestinationCreateWithURL((__bridge CFURLRef) out, job.png ? CFSTR("public.png") : CFSTR("public.jpeg"), 1, nullptr);
	bool ok = false;
	if (dst && image) {
		NSMutableDictionary* props = [@{(__bridge NSString*) kCGImagePropertyDPIWidth: @(job.dpi), (__bridge NSString*) kCGImagePropertyDPIHeight: @(job.dpi)} mutableCopy];
		if (!job.png) props[(__bridge NSString*) kCGImageDestinationLossyCompressionQuality] = @(std::max(1, std::min(100, job.quality)) / 100.0);
		CGImageDestinationAddImage(dst, image, (__bridge CFDictionaryRef) props);
		ok = CGImageDestinationFinalize(dst);
	}
	if (dst) CFRelease(dst);
	if (image) CGImageRelease(image);
	return ok ? "" : "couldn't write " + job.out;
}

} // namespace

namespace slippy {

std::string EncodeImage(const RasterJob& job)
{
	CGContextRef ctx = NewCanvas(job);
	if (!ctx) return TooBig(job);
	for (const std::string& file : job.images) {
		NSURL* in = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.c_str()]];
		CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef) in, nullptr);
		CGImageRef rendered = src ? CGImageSourceCreateImageAtIndex(src, 0, nullptr) : nullptr;
		if (src) CFRelease(src);
		if (!rendered) { CGContextRelease(ctx); return "couldn't read Illustrator's render"; }
		CGContextDrawImage(ctx, CGRectMake(0, 0, job.pixelsWide, job.pixelsHigh), rendered);
		CGImageRelease(rendered);
	}
	return Write(ctx, job);
}

} // namespace slippy

#endif
