// Symbols and isolation mode, straight from AISymbolSuite and
// AIIsolationModeSuite: make, place, swap, break, and edit a symbol's
// definition in place (the double-click in the UI), then save or discard.
// Symbols are named; instances are art ids like any other art.

#include "Kit.h"

#include <cmath>

namespace slippy {

namespace {

std::string SymbolName(AIPatternHandle symbol)
{
	ai::UnicodeString name;
	return sAISymbol->GetSymbolPatternName(symbol, name) ? "" : S(name);
}

AIPatternHandle SymbolByName(const std::string& name)
{
	Need(sAISymbol, "The symbol suite");
	ActiveDocument();
	AIPatternHandle symbol = nullptr;
	if (sAISymbol->GetSymbolPatternByName(U(name), &symbol) || !symbol)
		Fail(kErrNotFound, "no symbol named '" + name + "' in this document - see symbol.list");
	return symbol;
}

AIPatternHandle SymbolOfInstance(AIArtHandle art)
{
	if (ArtType(art) != kSymbolArt) Fail(kErrInvalidParams, "art " + ArtId(art) + " isn't a symbol instance");
	AIPatternHandle symbol = nullptr;
	Check(sAISymbol->GetSymbolPatternOfSymbolArt(art, &symbol), "GetSymbolPatternOfSymbolArt");
	return symbol;
}

// 'symbol' (a name), or the symbol of instance 'id'.
AIPatternHandle SymbolParam(const json::Value& p)
{
	if (p.get("symbol").isString()) return SymbolByName(p.get("symbol").asString());
	Need(sAISymbol, "The symbol suite");
	if (IsId(p.get("id"))) return SymbolOfInstance(ArtById(IdText(p.get("id"))));
	Fail(kErrInvalidParams, "pass 'symbol' (a name from symbol.list) or 'id' (an instance)");
}

AISymbolRegistrationPoint Registration(const json::Value& p)
{
	std::string r = Lower(p.str("registration", "center"));
	if (r == "topleft") return kSymbolTopLeftPoint;
	if (r == "top") return kSymbolTopMiddlePoint;
	if (r == "topright") return kSymbolTopRightPoint;
	if (r == "left") return kSymbolMiddleLeftPoint;
	if (r == "center") return kSymbolCenterPoint;
	if (r == "right") return kSymbolMiddleRightPoint;
	if (r == "bottomleft") return kSymbolBottomLeftPoint;
	if (r == "bottom") return kSymbolBottomMiddlePoint;
	if (r == "bottomright") return kSymbolBottomRightPoint;
	Fail(kErrInvalidParams, "'registration' must be center, top, bottom, left, right, topLeft, topRight, bottomLeft or bottomRight");
}

std::vector<AIArtHandle> Instances()
{
	std::vector<AIArtHandle> out;
	AIMatchingArtSpec spec(kSymbolArt, 0, 0);
	AIArtHandle** matches = nullptr;
	ai::int32 n = 0;
	if (sAIMatchingArt && !sAIMatchingArt->GetMatchingArt(&spec, 1, &matches, &n) && matches) {
		for (ai::int32 i = 0; i < n; i++) out.push_back((*matches)[i]);
		sSPBlocks->FreeBlock(matches);
	}
	return out;
}

// The pattern being edited: the named one, or the innermost editing session.
AIPatternHandle EditingSymbol(const json::Value& p)
{
	Need(sAISymbol, "The symbol suite");
	if (!sAISymbol->GetSymbolEditMode()) Fail(kErrInvalidParams, "no symbol is being edited - start with symbol.edit");
	if (p.get("symbol").isString()) {
		AIPatternHandle symbol = SymbolByName(p.get("symbol").asString());
		if (!sAISymbol->EditingSymbolPattern(symbol)) Fail(kErrInvalidParams, "'" + p.get("symbol").asString() + "' isn't being edited");
		return symbol;
	}
	AIPatternHandle found = nullptr;
	ai::int32 count = 0;
	sAISymbol->CountSymbolPatterns(&count, true);
	for (ai::int32 i = 0; i < count; i++) {
		AIPatternHandle symbol = nullptr;
		if (!sAISymbol->GetNthSymbolPattern(i, &symbol, true) && symbol && sAISymbol->EditingSymbolPattern(symbol)) found = symbol;
	}
	if (!found) Fail(kErrInvalidParams, "couldn't tell which symbol is being edited - pass 'symbol'");
	return found;
}

// What's editable now: the contents of the isolation layer.
json::Value EditableContents()
{
	json::Value list = json::Value::MakeArray();
	AILayerHandle layer = nullptr;
	AIArtHandle group = nullptr, child = nullptr;
	if (sAILayer->GetCurrentLayer(&layer) || !layer || sAIArt->GetFirstArtOfLayer(layer, &group) || !group) return list;
	for (sAIArt->GetArtFirstChild(group, &child); child; sAIArt->GetArtSibling(child, &child)) list.push(ArtSummary(child, 2));
	return list;
}

// ---- commands

json::Value SymbolList(const json::Value&)
{
	Need(sAISymbol, "The symbol suite");
	ActiveDocument();
	std::map<AIPatternHandle, int> uses;
	for (AIArtHandle a : Instances()) {
		AIPatternHandle s = nullptr;
		if (!sAISymbol->GetSymbolPatternOfSymbolArt(a, &s) && s) uses[s]++;
	}
	json::Value list = json::Value::MakeArray();
	ai::int32 count = 0;
	sAISymbol->CountSymbolPatterns(&count, false);
	for (ai::int32 i = 0; i < count; i++) {
		AIPatternHandle symbol = nullptr;
		if (sAISymbol->GetNthSymbolPattern(i, &symbol, false) || !symbol) continue;
		json::Value s;
		s["name"] = SymbolName(symbol);
		s["instances"] = uses[symbol];
		if (sAISymbol->EditingSymbolPattern(symbol)) s["editing"] = true;
		list.push(s);
	}
	return list;
}

json::Value SymbolInstances(const json::Value& p)
{
	Need(sAISymbol, "The symbol suite");
	ActiveDocument();
	AIPatternHandle want = p.get("symbol").isString() ? SymbolByName(p.get("symbol").asString()) : nullptr;
	json::Value list = json::Value::MakeArray();
	for (AIArtHandle a : Instances()) {
		AIPatternHandle s = nullptr;
		if (sAISymbol->GetSymbolPatternOfSymbolArt(a, &s) || (want && s != want)) continue;
		json::Value v = ArtSummary(a, 0);
		v["symbol"] = SymbolName(s);
		list.push(v);
	}
	return list;
}

json::Value SymbolCreate(const json::Value& p)
{
	Need(sAISymbol, "The symbol suite");
	std::vector<AIArtHandle> arts = ArtList(p, true);
	AIArtHandle top = arts.front();
	// From the art itself, not the selection: a selection made in the same
	// run isn't settled yet, and the symbol came out empty.
	AIArtHandle definition = top;
	if (arts.size() > 1) {
		Check(sAIArt->NewArt(kGroupArt, kPlaceAbove, top, &definition), "NewArt group");
		for (auto a = arts.rbegin(); a != arts.rend(); ++a) Check(sAIArt->ReorderArt(*a, kPlaceInsideOnTop, definition), "ReorderArt");
		arts = {definition};
		top = definition;
	}
	AIRealRect bounds;
	Check(sAIArt->GetArtBounds(definition, &bounds), "GetArtBounds");
	AIPatternHandle symbol = nullptr;
	Check(sAISymbol->NewSymbolPattern(&symbol, definition, Registration(p), true, false), "NewSymbolPattern");
	if (p.get("name").isString()) {
		ai::UnicodeString name = U(p.get("name").asString());
		if (sAISymbol->SetSymbolPatternName(symbol, name)) sAISymbol->SetSymbolPatternBaseName(symbol, name);   // taken: made unique
	}
	json::Value v;
	v["symbol"] = SymbolName(symbol);
	// Like Illustrator's New Symbol: the art becomes an instance of it.
	if (p.boolean("replace", true)) {
		AIRealPoint center;
		center.h = (bounds.left + bounds.right) / 2;
		center.v = (bounds.top + bounds.bottom) / 2;
		AIArtHandle instance = nullptr;
		Check(sAISymbol->NewInstanceAtLocation(symbol, center, kPlaceAbove, top, &instance), "NewInstanceAtLocation");
		for (AIArtHandle a : arts) sAIArt->DisposeArt(a);
		SelectOnly({instance});
		v["instance"] = Finish(instance, json::Value::MakeObject());
	}
	return v;
}

json::Value SymbolPlace(const json::Value& p)
{
	AIPatternHandle symbol = SymbolByName(ReqStr(p, "symbol"));
	ai::int16 order;
	AIArtHandle prep;
	Placement(p, order, prep);
	AIArtHandle instance = nullptr;
	if (p.has("at")) Check(sAISymbol->NewInstanceAtLocation(symbol, Point(p.get("at"), "at"), order, prep, &instance), "NewInstanceAtLocation");
	else Check(sAISymbol->NewInstanceCenteredInView(symbol, order, prep, &instance), "NewInstanceCenteredInView");
	json::Value v = Finish(instance, p);
	v["symbol"] = SymbolName(symbol);
	return v;
}

json::Value SymbolReplace(const json::Value& p)
{
	AIPatternHandle symbol = SymbolByName(ReqStr(p, "symbol"));
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		SymbolOfInstance(a);   // must be an instance
		Check(sAISymbol->SetSymbolPatternOfSymbolArt(a, symbol), "SetSymbolPatternOfSymbolArt");
		out.push(ArtSummary(a, 0));
	}
	return out;
}

json::Value SymbolBreak(const json::Value& p)
{
	Need(sAISymbol, "The symbol suite");
	json::Value out = json::Value::MakeArray();
	for (AIArtHandle a : ArtList(p, true)) {
		SymbolOfInstance(a);
		AIArtHandle expanded = nullptr;
		Check(sAISymbol->BreakLinkToSymbol(a, kPlaceAbove, a, &expanded), "BreakLinkToSymbol");
		sAIArt->DisposeArt(a);   // BreakLinkToSymbol leaves the instance
		if (expanded) out.push(ArtSummary(expanded, 1));
	}
	return out;
}

json::Value SymbolRename(const json::Value& p)
{
	AIPatternHandle symbol = SymbolParam(p);
	Check(sAISymbol->SetSymbolPatternName(symbol, U(ReqStr(p, "name"))), "SetSymbolPatternName (is the name taken?)");
	json::Value v;
	v["symbol"] = SymbolName(symbol);
	return v;
}

json::Value SymbolDelete(const json::Value& p)
{
	AIPatternHandle symbol = SymbolParam(p);
	std::string name = SymbolName(symbol);
	std::string instances = p.str("instances", "expand");
	if (instances != "expand" && instances != "delete") Fail(kErrInvalidParams, "'instances' must be \"expand\" or \"delete\"");
	Check(sAISymbol->DeleteSymbolPatternEx(symbol, instances == "delete" ? kDeleteInstances : kExpandInstances), "DeleteSymbolPattern");
	json::Value v;
	v["deleted"] = name;
	return v;
}

json::Value SymbolRedefine(const json::Value& p)
{
	AIPatternHandle symbol = SymbolByName(ReqStr(p, "symbol"));
	std::vector<AIArtHandle> arts = ArtList(p, true);
	if (arts.size() == 1) Check(sAISymbol->SetSymbolPatternArt(symbol, arts[0], false), "SetSymbolPatternArt");
	else {
		SelectOnly(arts);
		Check(sAISymbol->SetSymbolPatternFromSel(symbol), "SetSymbolPatternFromSel");
	}
	json::Value v;
	v["symbol"] = SymbolName(symbol);
	return v;
}

json::Value SymbolEdit(const json::Value& p)
{
	Need(sAISymbol, "The symbol suite");
	AIArtHandle instance = IsId(p.get("id")) ? ArtById(IdText(p.get("id"))) : nullptr;
	AIPatternHandle symbol = instance ? SymbolOfInstance(instance) : SymbolByName(ReqStr(p, "symbol"));
	if (sAISymbol->GetSymbolEditMode() || (sAIIsolationMode && sAIIsolationMode->IsInIsolationMode()))
		Fail(kErrInvalidParams, "already editing a symbol or isolated - symbol.finish (or isolation.exit) first");
	Check(sAISymbol->SetEditingSymbolDefinition(symbol, instance), "SetEditingSymbolDefinition");
	json::Value v;
	v["editing"] = SymbolName(symbol);
	v["contents"] = EditableContents();
	v["next"] = "edit these like any art, then symbol.finish (save) or symbol.finish {save: false}";
	return v;
}

// Ends a symbol edit, and never leaves the document stuck in one: if
// Illustrator won't end the session normally, leave isolation mode (which
// saves, as in the UI); if that fails too, discard the edit to get out.
json::Value SymbolFinish(const json::Value& p)
{
	Need(sAISymbol, "The symbol suite");
	ActiveDocument();
	bool save = p.boolean("save", true);
	bool isolated = sAIIsolationMode && sAIIsolationMode->IsInIsolationMode();
	if (!sAISymbol->GetSymbolEditMode() && !isolated) Fail(kErrInvalidParams, "no symbol is being edited");
	json::Value v;
	std::string why;
	AIPatternHandle symbol = nullptr;
	if (sAISymbol->GetSymbolEditMode()) {
		try { symbol = EditingSymbol(p); }
		catch (const CommandError& e) { why = e.message; }
	}
	auto stillIn = [] { return sAISymbol->GetSymbolEditMode() || (sAIIsolationMode && sAIIsolationMode->IsInIsolationMode()); };
	if (symbol) {
		v["symbol"] = SymbolName(symbol);
		AIErr e = sAISymbol->EndEditingSymbolDefinition(symbol, save);
		if (!e) {
			// Ending the edit can leave isolation mode on (30.2), and then nothing
			// outside it can be touched. Leave it the way the edit went.
			if (stillIn() && sAIIsolationMode && sAIIsolationMode->IsInIsolationMode()) {
				if (save) sAIIsolationMode->ExitIsolationMode(); else sAIIsolationMode->CancelIsolationMode();
			}
			if (sAISymbol->GetSymbolEditMode()) sAISymbol->ExitSymbolEditMode();
			if (!stillIn()) { v["saved"] = save; v["how"] = "ended the edit"; return v; }
			why = "the edit ended but Illustrator stayed in it";
		}
		else why = "EndEditingSymbolDefinition failed (" + ErrText(e) + ")";
	}
	if (save && sAIIsolationMode && sAIIsolationMode->IsInIsolationMode()) {
		AIErr e = sAIIsolationMode->ExitIsolationMode();
		if (!e && !sAISymbol->GetSymbolEditMode()) { v["saved"] = true; v["how"] = "left isolation mode, which saves the edit"; v["note"] = why; return v; }
		if (e) why += "; ExitIsolationMode failed (" + ErrText(e) + ")";
	}
	if (sAISymbol->GetSymbolEditMode()) {
		AIErr e = sAISymbol->ExitSymbolEditMode();   // discards every open symbol edit
		if (e) why += "; ExitSymbolEditMode failed (" + ErrText(e) + ")";
	}
	if (sAIIsolationMode && sAIIsolationMode->IsInIsolationMode()) sAIIsolationMode->CancelIsolationMode();
	if (sAISymbol->GetSymbolEditMode() || (sAIIsolationMode && sAIIsolationMode->IsInIsolationMode()))
		Fail(kErrIllustrator, "couldn't leave the symbol edit: " + why);
	v["saved"] = false;
	v["how"] = save ? "couldn't save, so the edit was discarded to get out" : "discarded the edit";
	if (!why.empty()) v["note"] = why;
	return v;
}

// ---- isolation

json::Value IsolationState(const json::Value&)
{
	Need(sAIIsolationMode, "The isolation mode suite");
	ActiveDocument();
	json::Value v;
	v["isolated"] = (bool) sAIIsolationMode->IsInIsolationMode();
	if (sAISymbol) v["editingSymbol"] = (bool) sAISymbol->GetSymbolEditMode();
	if (v.get("isolated").asBool()) {
		AIArtHandle parent = nullptr;
		sAIIsolationMode->GetIsolatedArtAndParents(&parent, nullptr);
		if (parent) v["originalParent"] = ArtId(parent);
		v["contents"] = EditableContents();
	}
	return v;
}

json::Value IsolationEnter(const json::Value& p)
{
	Need(sAIIsolationMode, "The isolation mode suite");
	AIArtHandle art = ArtList(p, true).front();
	if (!sAIIsolationMode->CanIsolateArt(art)) Fail(kErrInvalidParams, "art " + ArtId(art) + " can't be isolated (groups, symbols being edited and plug-in groups can)");
	Check(sAIIsolationMode->EnterIsolationMode(art, p.boolean("hideOthers", false)), "EnterIsolationMode");
	return IsolationState(p);
}

// Leaves isolation mode; during a symbol edit, finishes it (save=true by default).
json::Value IsolationExit(const json::Value& p)
{
	Need(sAIIsolationMode, "The isolation mode suite");
	ActiveDocument();
	if (sAISymbol && sAISymbol->GetSymbolEditMode()) return SymbolFinish(p);
	if (sAIIsolationMode->IsInIsolationMode()) {
		AIErr e = sAIIsolationMode->ExitIsolationMode();
		if (e) { sAIIsolationMode->CancelIsolationMode(); if (sAIIsolationMode->IsInIsolationMode()) Check(e, "ExitIsolationMode"); }
	}
	json::Value v;
	v["isolated"] = false;
	return v;
}

} // namespace

void AddSymbolCommands(CommandTable& t)
{
	const char* sym = "string - the symbol's name (symbol.list)";
	t["symbol.list"] = {"The document's symbols: name, how many instances, whether one's being edited.", Params({}), SymbolList, false};
	t["symbol.instances"] = {"Symbol instances (art ids), all or of one symbol.", Params({{"symbol", sym}}), SymbolInstances, false};
	t["symbol.create"] = {"New symbol from art (default: the selection). Like Object > New Symbol, the art is replaced by an instance unless replace=false.",
		Params({{"ids", kIds}, {"id", "string"}, {"name", "string"}, {"registration", "center (default) | top | bottom | left | right | topLeft | topRight | bottomLeft | bottomRight"},
			{"replace", "boolean (default true)"}}), SymbolCreate, true};
	t["symbol.place"] = {"Place an instance of a symbol, centered 'at' a point (default: the middle of the view).",
		Params({{"symbol", sym}, {"at", "[x, y]"}, {"name", "string"}, {"layer", kWhere}, {"parent", kWhere}, {"select", "boolean"}}), SymbolPlace, true};
	t["symbol.replace"] = {"Swap instances (default: the selection) to another symbol, keeping their transforms.",
		Params({{"symbol", sym}, {"ids", kIds}, {"id", "string"}}), SymbolReplace, true};
	t["symbol.break"] = {"Break the link: instances (default: the selection) become plain art. Returns the new art.",
		Params({{"ids", kIds}, {"id", "string"}}), SymbolBreak, true};
	t["symbol.edit"] = {"Edit a symbol's definition in place (isolation mode, like double-clicking an instance). Returns the editable art; "
		"change it with any command, then symbol.finish.", Params({{"id", "string - an instance to edit in place"}, {"symbol", "string - the symbol's name, instead of id"}}), SymbolEdit, true};
	t["symbol.finish"] = {"End a symbol edit: save=true (default) updates every instance, false discards the changes. If Illustrator won't end it "
		"normally it leaves isolation mode (saving) or, failing that, discards the edit - 'how' says which.",
		Params({{"save", "boolean (default true)"}, {"symbol", "string - which, when editing nested symbols"}}), SymbolFinish, true};
	t["symbol.redefine"] = {"Replace a symbol's definition with art (default: the selection); every instance updates.",
		Params({{"symbol", sym}, {"ids", kIds}, {"id", "string"}}), SymbolRedefine, true};
	t["symbol.rename"] = {"Rename a symbol.", Params({{"symbol", sym}, {"id", "string - or an instance"}, {"name", "string - the new name"}}), SymbolRename, true};
	t["symbol.delete"] = {"Delete a symbol; its instances are expanded into plain art (default) or deleted.",
		Params({{"symbol", sym}, {"id", "string - or an instance"}, {"instances", "\"expand\" (default) | \"delete\""}}), SymbolDelete, true};
	t["isolation.state"] = {"Whether the document is in isolation mode (or a symbol edit), and what's editable.", Params({}), IsolationState, false};
	t["isolation.enter"] = {"Isolate a group (default: the selected one), like double-clicking it; everything else is dimmed (or hidden with hideOthers).",
		Params({{"id", "string"}, {"hideOthers", "boolean"}}), IsolationEnter, true};
	t["isolation.exit"] = {"Leave isolation mode; during a symbol edit this finishes it (save=true by default, false discards).",
		Params({{"save", "boolean - symbol edits only (default true)"}}), IsolationExit, true};
}

} // namespace slippy
