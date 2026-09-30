#include "Raster.h"

#include <algorithm>
#include <string>

#ifdef _WIN32

// Windows: Windows.Data.Pdf draws the page (the same renderer as Edge's PDF
// view), WIC writes the file with its resolution and JPEG quality. It runs on
// its own thread in a multithreaded apartment, so blocking on the WinRT calls
// never stalls Illustrator's UI apartment.

#include <windows.h>
#include <shcore.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Data.Pdf.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.h>

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

std::string Render(const slippy::RasterJob& job)
{
	using namespace winrt::Windows;
	Storage::StorageFile file = Storage::StorageFile::GetFileFromPathAsync(Wide(job.pdf)).get();
	Data::Pdf::PdfDocument doc = Data::Pdf::PdfDocument::LoadFromFileAsync(file).get();
	if ((int) doc.PageCount() <= job.page) return "Illustrator's PDF had no page " + std::to_string(job.page);
	Data::Pdf::PdfPage page = doc.GetPage((uint32_t) job.page);

	// The page's size and the source rect are in DIPs (1/96"), from the top left.
	const double k = 96.0 / 72.0;
	Foundation::Size size = page.Size();
	Data::Pdf::PdfPageRenderOptions options;
	options.SourceRect(Foundation::Rect((float) (job.left * k), (float) (size.Height - (job.bottom + job.height) * k),
		(float) (job.width * k), (float) (job.height * k)));
	options.DestinationWidth((uint32_t) job.pixelsWide);
	options.DestinationHeight((uint32_t) job.pixelsHigh);
	bool clear = job.png && job.transparent;
	options.BackgroundColor(UI::Color{(uint8_t) (clear ? 0 : 255), 255, 255, 255});
	options.BitmapEncoderId(Graphics::Imaging::BitmapEncoder::PngEncoderId());
	Storage::Streams::InMemoryRandomAccessStream rendered;
	page.RenderToStreamAsync(rendered, options).get();
	rendered.Seek(0);

	winrt::com_ptr<IStream> in;
	HRESULT hr = CreateStreamOverRandomAccessStream(winrt::get_unknown(rendered), IID_PPV_ARGS(in.put()));
	if (FAILED(hr)) return Hr("CreateStreamOverRandomAccessStream", hr);
	winrt::com_ptr<IWICImagingFactory> wic;
	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put()));
	if (FAILED(hr)) return Hr("WIC", hr);
	winrt::com_ptr<IWICBitmapDecoder> decoder;
	winrt::com_ptr<IWICBitmapFrameDecode> frame;
	if (FAILED(hr = wic->CreateDecoderFromStream(in.get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.put()))) return Hr("Reading the render", hr);
	if (FAILED(hr = decoder->GetFrame(0, frame.put()))) return Hr("Reading the render", hr);

	WICPixelFormatGUID format = job.png ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
	winrt::com_ptr<IWICFormatConverter> converted;
	if (FAILED(hr = wic->CreateFormatConverter(converted.put()))) return Hr("CreateFormatConverter", hr);
	if (FAILED(hr = converted->Initialize(frame.get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return Hr("Converting the render", hr);

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

namespace slippy {

std::string RenderPdfPage(const RasterJob& job)
{
	std::string error;
	std::thread worker([&] {
		try {
			winrt::init_apartment(winrt::apartment_type::multi_threaded);
			error = Render(job);
		}
		catch (const winrt::hresult_error& e) {
			error = "Windows couldn't draw the PDF: " + winrt::to_string(e.message());
		}
		catch (...) {
			error = "Windows couldn't draw the PDF";
		}
		winrt::uninit_apartment();
	});
	worker.join();
	return error;
}

} // namespace slippy

#else // macOS

// Core Graphics draws the page at the asked resolution; ImageIO writes it.

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

namespace slippy {

std::string RenderPdfPage(const RasterJob& job)
{
	NSURL* pdfURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:job.pdf.c_str()]];
	CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL((__bridge CFURLRef) pdfURL);
	CGPDFPageRef page = doc ? CGPDFDocumentGetPage(doc, (size_t) job.page + 1) : nullptr;
	if (!page) { if (doc) CGPDFDocumentRelease(doc); return "Illustrator's PDF had no page " + std::to_string(job.page); }
	CGRect box = CGPDFPageGetBoxRect(page, kCGPDFCropBox);

	size_t w = job.pixelsWide, h = job.pixelsHigh;
	double k = job.dpi / 72.0;
	CGColorSpaceRef rgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, 0, rgb, (CGBitmapInfo) kCGImageAlphaPremultipliedLast);
	CGColorSpaceRelease(rgb);
	if (!ctx) { CGPDFDocumentRelease(doc); return "not enough memory for a " + std::to_string(w) + " x " + std::to_string(h) + " image"; }
	if (!(job.png && job.transparent)) { CGContextSetRGBFillColor(ctx, 1, 1, 1, 1); CGContextFillRect(ctx, CGRectMake(0, 0, w, h)); }
	CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
	CGContextScaleCTM(ctx, k, k);
	// Page space starts at the crop box's bottom-left.
	CGContextTranslateCTM(ctx, -(box.origin.x + job.left), -(box.origin.y + job.bottom));
	CGContextDrawPDFPage(ctx, page);
	CGImageRef image = CGBitmapContextCreateImage(ctx);
	CGContextRelease(ctx);
	CGPDFDocumentRelease(doc);

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

} // namespace slippy

#endif
