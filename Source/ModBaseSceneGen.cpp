/**
 * @file ModBaseSceneGen.cpp
 * @brief Generate / update a Mod Settings scene (see ModBaseSceneGen.h).
 */

#include "ModBaseSceneGen.h"

#if EDITOR

#include "ModBaseDisplay.h"
#include "ModBaseLauncher.h"
#include "ModBaseModMap.h"
#include "ModBaseSettings.h"
#include "ModBaseUiBuilder.h"

#include "AssetDir.h"
#include "AssetManager.h"
#include "Assets/Scene.h"
#include "Editor/EditorUtils.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace
{
using namespace RecompUi;

// Sizes in UI pixels. The panel fills the screen minus kMargin, at most kMaxPanel (a
// 640x480 Wii / GameCube screen gets 608x448; PC screens a centred 760x560 panel).
const float kRowH = 30.0f;
const float kRowGap = 4.0f;
const float kPad = 12.0f;
const float kGap = 6.0f;
const float kTitleH = 24.0f;
const float kTabW = 120.0f;
const float kMargin = 16.0f;
const glm::vec2 kMaxPanel = {760.0f, 560.0f};
const float kValueW = 64.0f;
const float kStepW = 40.0f;

std::string NodeName(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        out += (std::isalnum((unsigned char)c) || c == '_' || c == '.') ? c : '_';
    }
    return out.empty() ? std::string("_") : out;
}

// The format a Display entry shows its value with.
std::string ValueFormat(const ModEntry& e)
{
    std::string token;
    if (e.mSource == ModSource::Variable)
    {
        token = "{" + e.mName + (e.mIndex != 0 ? "[" + std::to_string(e.mIndex) + "]" : std::string()) + "}";
    }
    else
    {
        token = "{@" + e.mId + "}";
    }
    if (e.mFormat.empty())
    {
        return token;
    }
    std::string f = e.mFormat;
    const size_t v = f.find("{v}");
    if (v != std::string::npos)
    {
        f.replace(v, 3, token);
    }
    return f;
}

// One row of a page's list: an ArrayWidget row as wide as the list. The label (or the
// single button) takes the width the fixed-size parts leave, so rows follow the panel.
// Returns the row's buttons (left to right) for gamepad navigation.
std::vector<Button*> MakeRow(Builder& b, Node* list, const ModEntry& e)
{
    std::vector<Button*> buttons;
    const std::string label = e.mLabel.empty() ? e.mId : e.mLabel;
    const std::string shown = label + (e.mSource == ModSource::StartupOption ? " *" : "");
    Widget* row = Array(b, list, "Row_" + NodeName(e.mId), true, 4.0f, 0.0f, FullWidth(kRowH), true);
    if (row == nullptr)
    {
        return buttons;
    }
    switch (e.mKind)
    {
    case ModKind::Toggle:
    case ModKind::Choice:
        buttons.push_back(SettingButton(b, row, "Button", shown + ": {@" + e.mId + "}", e.mId, 1, FullWidth(kRowH)));
        break;
    case ModKind::Int:
    case ModKind::Float:
        Label(b, row, "Label", shown, FullWidth(kRowH));
        Bound(b, row, "Value", "{@" + e.mId + "}", At(0, 0, kValueW, kRowH), kFontSize, kHeaderColor);
        buttons.push_back(SettingButton(b, row, "Minus", "-", e.mId, -1, At(0, 0, kStepW, kRowH)));
        buttons.push_back(SettingButton(b, row, "Plus", "+", e.mId, 1, At(0, 0, kStepW, kRowH)));
        break;
    case ModKind::Action:
        buttons.push_back(SettingButton(b, row, "Button", shown, e.mId, 1, FullWidth(kRowH)));
        break;
    case ModKind::Display:
        Label(b, row, "Label", shown, FullWidth(kRowH), kFontSize, kDimColor);
        Bound(b, row, "Value", ValueFormat(e), At(0, 0, 220.0f, kRowH));
        break;
    case ModKind::Bar:
        Label(b, row, "Label", shown, At(0, 0, 140.0f, kRowH), kFontSize, kDimColor);
        Bar(b, row, "Bar", e.mName, e.mMaxName, FullWidth(kRowH - 12.0f, 6.0f), {0.35f, 0.8f, 0.35f, 1.0f});
        break;
    default:
        break;
    }
    return buttons;
}

// A panel this version can't update in place, so it is rebuilt: the first fixed-size
// layout (Pages directly in the panel), or the first responsive one (plain-widget rows,
// pixel-margin stretches that collapsed after a save).
bool IsOutdatedPanel(Node* panel)
{
    if (panel == nullptr)
    {
        return false;
    }
    Node* layout = panel->FindChild("Layout", false);
    if (layout == nullptr)
    {
        return panel->FindChild("Pages", false) != nullptr;
    }
    Node* pages = layout->FindChild("Pages", false);
    for (uint32_t p = 0; pages != nullptr && p < pages->GetNumChildren(); ++p)
    {
        Node* list = pages->GetChild((int32_t)p)->FindChild("List", false);
        for (uint32_t r = 0; list != nullptr && r < list->GetNumChildren(); ++r)
        {
            if (std::strcmp(list->GetChild((int32_t)r)->RuntimeName(), "ArrayWidget") != 0) return true;
        }
    }
    return false;
}

// Generated scenes go to the project's own Assets/Scenes (not the game package, which is a
// shared, git-managed package): they belong to the project that uses them.
AssetDir* SceneDir(std::string& outError)
{
    AssetManager* am = AssetManager::Get();
    AssetDir* project = am ? am->FindProjectDirectory() : nullptr;
    if (project == nullptr)
    {
        outError = "No project is open.";
        return nullptr;
    }
    AssetDir* scenes = project->GetSubdirectory("Scenes");
    if (scenes == nullptr)
    {
        scenes = project->CreateSubdirectory("Scenes");
    }
    if (scenes == nullptr)
    {
        outError = "Cannot create the Scenes folder in " + project->mPath;
    }
    return scenes;
}
}

std::string ModScene_DefaultName(const ModMap* map)
{
    std::string title = map ? (map->mTitle.empty() ? map->SaveName() : map->mTitle) : std::string("Game");
    std::string name = "SC_";
    bool upper = true;
    for (char c : title)
    {
        if (std::isalnum((unsigned char)c))
        {
            name += upper ? (char)std::toupper((unsigned char)c) : c;
            upper = false;
        }
        else
        {
            upper = true;
        }
    }
    return name + "ModSettings";
}

bool ModScene_Generate(ModMap* map, const ModSceneOptions& options, std::string& outMessage)
{
    if (map == nullptr)
    {
        outMessage = "No Mod Map selected.";
        return false;
    }
    const std::string sceneName = options.sceneName.empty() ? ModScene_DefaultName(map) : options.sceneName;

    // existing scene: update it in place; else a new tree
    Scene* scene = nullptr;
    AssetStub* stub = FetchAssetStub(sceneName);
    if (stub != nullptr && stub->mType != Scene::GetStaticType())
    {
        outMessage = "An asset named " + sceneName + " exists and is not a Scene.";
        return false;
    }
    NodePtr root;
    if (stub != nullptr)
    {
        scene = LoadAsset<Scene>(sceneName);
        if (scene != nullptr)
        {
            root = scene->Instantiate();
        }
    }
    const bool updating = root.Get() != nullptr;
    if (!updating)
    {
        SharedPtr<Canvas> canvas = Node::Construct<Canvas>();
        canvas->SetName("ModSettings");
        canvas->SetFullScreen();
        root = PtrStaticCast<Node>(canvas);
    }
    if (root->As<Widget>() == nullptr)
    {
        outMessage = sceneName + "'s root is not a widget: left as it is.";
        return false;
    }

    // the entries by group (display settings last)
    std::vector<std::string> groups = map->OrderedGroups();
    std::vector<std::vector<const ModEntry*>> byGroup(groups.size());
    for (const ModEntry& e : map->mEntries)
    {
        const size_t g = std::find(groups.begin(), groups.end(), e.mGroup) - groups.begin();
        byGroup[g].push_back(&e);
    }
    if (options.includeDisplay)
    {
        std::vector<const ModEntry*> display;
        for (const char* id : {"display.mode", "display.scale", "display.filter", "display.resolution", "display.window"})
        {
            if (std::strcmp(id, "display.resolution") == 0 && Recomp_MaxResolution() <= 1)
            {
                continue; // no runtime here can draw larger
            }
            if (map->Find(id) == nullptr)
            {
                if (const ModEntry* e = ModSettings::Get().FindEntry(id))
                {
                    display.push_back(e);
                }
            }
        }
        if (!display.empty())
        {
            auto it = std::find(groups.begin(), groups.end(), std::string("Display"));
            if (it == groups.end())
            {
                groups.push_back("Display");
                byGroup.push_back(display);
            }
            else
            {
                auto& list = byGroup[it - groups.begin()];
                list.insert(list.end(), display.begin(), display.end());
            }
        }
    }
    for (size_t g = 0; g < groups.size();)
    {
        if (byGroup[g].empty())
        {
            groups.erase(groups.begin() + g);
            byGroup.erase(byGroup.begin() + g);
        }
        else
        {
            ++g;
        }
    }
    if (groups.empty())
    {
        outMessage = "The Mod Map has no entries.";
        return false;
    }

    Builder b;
    std::string rebuilt;
    if (Node* old = root->FindChild("Panel", false); IsOutdatedPanel(old))
    {
        root->RemoveChild(old);
        rebuilt = " The panel was rebuilt with the current scrolling layout.";
    }
    // the panel fills the screen; in play the menu controller insets it (margin, max size)
    Quad* panel = b.Ensure<Quad>(root.Get(), "Panel", [&](Quad* q) {
        Full(q);
        q->SetColor(kPanelColor);
    });
    if (panel == nullptr)
    {
        outMessage = "Panel exists but is not a Quad: left as it is.";
        return false;
    }
    Widget* layout = Array(b, panel, "Layout", false, kGap, kPad, Filled());
    if (layout == nullptr)
    {
        outMessage = "Panel/Layout exists but is not an ArrayWidget (or the engine has none): left as it is.";
        return false;
    }
    Label(b, layout, "Title", map->mTitle.empty() ? std::string("Mod Settings") : map->mTitle, FullWidth(kTitleH), 20.0f,
          kHeaderColor);

    // tabs: a row that scrolls sideways when the groups don't fit
    ScrollContainer* tabStrip = Scroll(b, layout, "TabStrip", true, FullWidth(kRowH));
    Widget* tabs = Array(b, tabStrip, "Tabs", true, 4.0f, 0.0f, At(0, 0, 0, kRowH));
    std::vector<Button*> tabButtons;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const std::string group = groups[g].empty() ? std::string("General") : groups[g];
        tabButtons.push_back(SettingButton(b, tabs, "Tab_" + NodeName(group), group, "@page:" + NodeName(group), 1,
                                           At(0, 0, kTabW, kRowH)));
    }
    if (tabs != nullptr)
    {
        tabs->SetWidth(float(groups.size()) * (kTabW + 4.0f));
    }

    // pages: each a vertical scroll list taking the panel's remaining height
    Widget* pages = b.Ensure<Widget>(layout, "Pages", [](Widget* w) { FillRest(w); });
    std::vector<Button*> pageFirstButtons(groups.size(), nullptr);
    std::vector<std::vector<Button*>> pageButtons(groups.size());
    for (size_t g = 0; g < groups.size() && pages != nullptr; ++g)
    {
        const std::string group = groups[g].empty() ? std::string("General") : groups[g];
        ScrollContainer* page = Scroll(b, pages, "Page_" + NodeName(group), false, [&](Widget* w) {
            Full(w);
            w->SetVisible(g == 0);
        });
        Widget* list = Array(b, page, "List", false, kRowGap, 0.0f, At(0, 0, 0, kRowH));
        if (list == nullptr)
        {
            continue;
        }
        std::vector<std::vector<Button*>> rows;
        for (const ModEntry* e : byGroup[g])
        {
            std::vector<Button*> row = MakeRow(b, list, *e);
            if (!row.empty())
            {
                rows.push_back(row);
            }
        }
        // the list is as tall as its rows (an ArrayWidget doesn't size itself)
        const float n = float(list->GetNumChildren());
        list->SetHeight(std::max(kRowH, n * (kRowH + kRowGap) - kRowGap));
        LinkNavigation(rows);
        if (!rows.empty())
        {
            pageFirstButtons[g] = rows.front().front();
            for (Button* btn : rows.front())
            {
                if (btn->GetNavUp() == nullptr) btn->SetNavUp(tabButtons[g]);
            }
            for (const auto& row : rows) pageButtons[g].insert(pageButtons[g].end(), row.begin(), row.end());
        }
    }
    LinkNavigation({tabButtons});

    // footer
    Widget* footer = Array(b, layout, "Footer", true, kGap, 0.0f, FullWidth(kRowH));
    Button* save = SettingButton(b, footer, "Save", "Save", "@save", 1, At(0, 0, 110.0f, kRowH));
    Button* reset = SettingButton(b, footer, "Reset", "Reset to defaults", "@reset", 1, At(0, 0, 180.0f, kRowH));
    Button* close = SettingButton(b, footer, "Close", "Close", "@close", 1, At(0, 0, 110.0f, kRowH));
    LinkNavigation({{save, reset, close}});
    // tab -> its page's first button (else the footer); the page's buttons without a down
    // link (its last row) -> Save. Footer -> up is set when a page is shown (ShowPage).
    for (size_t g = 0; g < tabButtons.size(); ++g)
    {
        Button* tab = tabButtons[g];
        if (tab != nullptr && tab->GetNavDown() == nullptr)
        {
            tab->SetNavDown(pageFirstButtons[g] != nullptr ? pageFirstButtons[g] : save);
        }
        for (Button* btn : pageButtons[g])
        {
            if (btn->GetNavDown() == nullptr && save != nullptr) btn->SetNavDown(save);
        }
    }
    for (Button* btn : {save, reset, close})
    {
        if (btn != nullptr && btn->GetNavUp() == nullptr && !tabButtons.empty())
        {
            btn->SetNavUp(pageFirstButtons[0] != nullptr ? pageFirstButtons[0] : tabButtons.front());
        }
    }
    bool anyStartup = false;
    for (const ModEntry& e : map->mEntries)
    {
        anyStartup = anyStartup || e.mSource == ModSource::StartupOption;
    }
    if (anyStartup)
    {
        Label(b, layout, "Note", "* applies when the game restarts", FullWidth(16.0f), 13.0f, kDimColor);
    }

    // controller
    RecompMenuController* controller = b.Ensure<RecompMenuController>(root.Get(), "MenuController", [&](RecompMenuController* c) {
        Place(c, 0.0f, 0.0f, 0.0f, 0.0f);
        c->Setup(map->mTitle.empty() ? std::string("Mod Settings") : map->mTitle, false, true,
                 tabButtons.empty() ? nullptr : tabButtons.front());
    });
    // "Open with" is chosen in the dialog each time, so it applies on Update too
    if (controller != nullptr)
    {
        // the controller hides the panel, never the root: hidden widgets don't tick, so
        // the root (and the controller in it) must stay visible to see the open button
        controller->SetPanel(panel);
        controller->SetPanelFit(kMaxPanel, kMargin, options.position);
        root->SetVisible(true);
        controller->SetToggleButton(options.toggleButton);
        controller->SetToggleAction(options.toggleAction);
    }

    // the map's style (Menu Style window) over everything generated or kept
    ModStyle_Apply(root.Get(), map->mStyle);

    // rows of entries no longer in the map: reported, not deleted (they may be the user's)
    std::vector<std::string> stale;
    for (uint32_t p = 0; pages != nullptr && p < pages->GetNumChildren(); ++p)
    {
        Node* page = pages->GetChild((int32_t)p);
        Node* list = page->FindChild("List", false);
        for (uint32_t r = 0; list != nullptr && r < list->GetNumChildren(); ++r)
        {
            const std::string& name = list->GetChild((int32_t)r)->GetName();
            if (name.compare(0, 4, "Row_") != 0) continue;
            bool found = false;
            for (const ModEntry& e : map->mEntries)
            {
                found = found || ("Row_" + NodeName(e.mId)) == name;
            }
            found = found || name.compare(0, 12, "Row_display.") == 0;
            if (!found) stale.push_back(page->GetName() + "/" + name);
        }
    }

    // save the scene
    if (!updating)
    {
        std::string error;
        AssetDir* dir = SceneDir(error);
        if (dir == nullptr)
        {
            outMessage = error;
            return false;
        }
        stub = EditorAddUniqueAsset(sceneName.c_str(), dir, Scene::GetStaticType(), true);
        scene = (stub && stub->mAsset) ? stub->mAsset->As<Scene>() : nullptr;
        if (scene == nullptr)
        {
            outMessage = "Cannot create the Scene asset " + sceneName;
            return false;
        }
    }
    scene->Capture(root.Get());
    AssetManager::Get()->SaveAsset(*stub);

    outMessage = std::string(updating ? "Updated " : "Created ") + stub->mName + ": " + std::to_string(b.added) +
                 " node(s) added" + (updating ? ", " + std::to_string(b.kept) + " kept as they were" : std::string()) +
                 "." + rebuilt + " Instance it in your game's scene.";
    if (!stale.empty())
    {
        outMessage += " Rows whose entries are gone from the map (delete them if you don't want them):";
        for (const std::string& s : stale)
        {
            outMessage += " " + s;
        }
    }
    LogDebug("Mods: %s", outMessage.c_str());
    return true;
}

namespace
{
// Settings UIs in a live tree: nodes with a RecompMenuController and a Panel child.
int RestyleInstances(Node* node, const ModStyle& style)
{
    if (node == nullptr)
    {
        return 0;
    }
    bool controller = false;
    for (uint32_t i = 0; i < node->GetNumChildren() && !controller; ++i)
    {
        controller = node->GetChild((int32_t)i)->As<RecompMenuController>() != nullptr;
    }
    if (controller && node->FindChild("Panel", false) != nullptr)
    {
        ModStyle_Apply(node, style);
        return 1;
    }
    int count = 0;
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        count += RestyleInstances(node->GetChild((int32_t)i), style);
    }
    return count;
}
}

int ModScene_RestyleOpen(const ModStyle& style)
{
    World* world = GetWorld(0);
    return world != nullptr ? RestyleInstances(world->GetRootNode(), style) : 0;
}

bool ModScene_ApplyStyle(ModMap* map, const std::string& sceneName, std::string& outMessage)
{
    if (map == nullptr)
    {
        outMessage = "No Mod Map selected.";
        return false;
    }
    const std::string name = sceneName.empty() ? ModScene_DefaultName(map) : sceneName;
    AssetStub* stub = FetchAssetStub(name);
    Scene* scene = (stub != nullptr && stub->mType == Scene::GetStaticType()) ? LoadAsset<Scene>(name) : nullptr;
    if (scene == nullptr)
    {
        outMessage = "No scene " + name + ": generate it first.";
        return false;
    }
    NodePtr root = scene->Instantiate();
    if (root.Get() == nullptr)
    {
        outMessage = "Cannot open the scene " + name + ".";
        return false;
    }
    ModStyle_Apply(root.Get(), map->mStyle);
    scene->Capture(root.Get());
    AssetManager::Get()->SaveAsset(*stub);
    const int live = ModScene_RestyleOpen(map->mStyle);
    outMessage = "Restyled " + name + (live > 0 ? " and " + std::to_string(live) + " instance(s) open in the editor." : ".");
    LogDebug("Mods: %s", outMessage.c_str());
    return true;
}

// ---- launcher ----------------------------------------------------------------------------------
namespace
{
const float kLauncherButtonH = 34.0f;
const float kLauncherGap = 8.0f;

// The scene asset of that name opened for updating, or a new root; `updating` says which.
NodePtr OpenOrCreate(const std::string& sceneName, AssetStub*& stub, Scene*& scene, bool& updating,
                     const char* rootName, std::string& outMessage)
{
    stub = FetchAssetStub(sceneName);
    scene = nullptr;
    if (stub != nullptr && stub->mType != Scene::GetStaticType())
    {
        outMessage = "An asset named " + sceneName + " exists and is not a Scene.";
        return NodePtr();
    }
    NodePtr root;
    if (stub != nullptr)
    {
        scene = LoadAsset<Scene>(sceneName);
        if (scene != nullptr) root = scene->Instantiate();
    }
    updating = root.Get() != nullptr;
    if (!updating)
    {
        SharedPtr<Canvas> canvas = Node::Construct<Canvas>();
        canvas->SetName(rootName);
        canvas->SetFullScreen();
        root = PtrStaticCast<Node>(canvas);
    }
    return root;
}

int RestyleLaunchers(Node* node, const ModMap& map)
{
    if (node == nullptr) return 0;
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        if (node->GetChild((int32_t)i)->As<RecompLauncher>() != nullptr)
        {
            ModLauncher_ApplyLook(node, map);
            return 1;
        }
    }
    int count = 0;
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        count += RestyleLaunchers(node->GetChild((int32_t)i), map);
    }
    return count;
}
}

std::string ModLauncher_DefaultName(const ModMap* map)
{
    std::string name = ModScene_DefaultName(map);
    const size_t at = name.rfind("ModSettings");
    if (at != std::string::npos) name.resize(at);
    return name + "Launcher";
}

bool ModLauncher_Generate(ModMap* map, const std::string& sceneNameIn, std::string& outMessage)
{
    if (map == nullptr)
    {
        outMessage = "No Mod Map selected.";
        return false;
    }
    const std::string sceneName = sceneNameIn.empty() ? ModLauncher_DefaultName(map) : sceneNameIn;
    AssetStub* stub = nullptr;
    Scene* scene = nullptr;
    bool updating = false;
    NodePtr root = OpenOrCreate(sceneName, stub, scene, updating, "Launcher", outMessage);
    if (root.Get() == nullptr)
    {
        return false;
    }
    if (root->As<Widget>() == nullptr)
    {
        outMessage = sceneName + "'s root is not a widget: left as it is.";
        return false;
    }
    const ModLauncherSettings& l = map->mLauncher;

    Builder b;
    b.Ensure<Quad>(root.Get(), "Background", [&](Quad* q) { Full(q); });
    // The panel is a container that draws nothing, its fill a Quad of its own under the content:
    // a widget's alpha (its color's) multiplies into its children's, so a translucent (or no)
    // fill on the content's parent would fade the content too.
    if (Node* old = root->FindChild("Panel", false); old != nullptr && old->As<Quad>() != nullptr)
    {
        // a launcher made before: its Panel Quad becomes the fill, its content moves to a new Panel
        int32_t index = 0;
        for (uint32_t i = 0; i < root->GetNumChildren(); ++i)
        {
            if (root->GetChild((int32_t)i) == old) index = (int32_t)i;
        }
        old->SetName("PanelBackground");
        Widget* container = root->CreateChild<Widget>("Panel");
        if (container != nullptr)
        {
            Full(container);
            container->Attach(root.Get(), false, index);
            while (old->GetNumChildren() > 0)
            {
                old->GetChild(0)->Attach(container);
            }
            old->Attach(container, false, 0);
            if (Widget* fill = old->As<Widget>()) Full(fill);
            ++b.added;
        }
    }
    Widget* panel = b.Ensure<Widget>(root.Get(), "Panel", [&](Widget* w) { Full(w); });
    if (panel == nullptr || panel->As<Quad>() != nullptr)
    {
        outMessage = "Panel exists but is not a plain widget: left as it is.";
        return false;
    }
    // (first: drawn under the content)
    b.Ensure<Quad>(panel, "PanelBackground", [&](Quad* q) {
        Full(q);
        q->SetColor(kPanelColor);
        q->Attach(panel, false, 0);
    });
    // the panel's content: what scrolls, and under it the footer
    Widget* body = Array(b, panel, "Body", false, 0.0f, 0.0f, Filled());
    if (body == nullptr)
    {
        outMessage = "Panel/Body exists but is not an ArrayWidget (or the engine has none): left as it is.";
        return false;
    }
    if (Node* old = panel->FindChild("Scroll", false))
    {
        // a launcher made before the footer: its content moves into the column
        old->Attach(body);
    }
    // the content scrolls when the window is too short for it (ModLauncher_ApplyLook sizes it)
    ScrollContainer* scroll = Scroll(b, body, "Scroll", false, [](Widget* w) { FillRest(w); });
    if (scroll == nullptr)
    {
        outMessage = "Panel/Body/Scroll exists but is not a ScrollContainer: left as it is.";
        return false;
    }
    if (Node* old = panel->FindChild("Layout", false))
    {
        // a launcher made before it scrolled: its content moves into the scroll view
        old->Attach(scroll);
    }
    Widget* layout = Array(b, scroll, "Layout", false, kLauncherGap, 16.0f, At(0.0f, 0.0f, 0.0f, 400.0f), true);
    if (layout == nullptr)
    {
        outMessage = "Panel/Scroll/Layout exists but is not an ArrayWidget (or the engine has none): left as it is.";
        return false;
    }
    b.Ensure<Quad>(layout, "Logo", [&](Quad* q) { Place(q, 0, 0, l.mLogoSize.x, l.mLogoSize.y); });
    auto centred = [](Text* t) {
        if (t != nullptr) t->SetHorizontalJustification(Justification::Center);
    };
    centred(Label(b, layout, "Title", "Game", FullWidth(40.0f), 28.0f, kHeaderColor));
    centred(Label(b, layout, "Subtitle", "", FullWidth(22.0f), kFontSize, kDimColor));
    centred(Bound(b, layout, "Rom", "{@launcher.romfile}", FullWidth(24.0f), kFontSize, kHeaderColor));
    centred(Bound(b, layout, "Message", "{@launcher.message}", FullWidth(40.0f), 13.0f, kDimColor));

    Widget* buttons = Array(b, layout, "Buttons", false, 6.0f, 0.0f, FullWidth(5 * (kLauncherButtonH + 6.0f)));
    RecompButton* play = SettingButton(b, buttons, "Play", "Play", "@launcher:play", 1, FullWidth(kLauncherButtonH));
    RecompButton* browse = SettingButton(b, buttons, "Browse", "Choose ROM...", "@launcher:browse", 1, FullWidth(kLauncherButtonH));
    RecompButton* forget = SettingButton(b, buttons, "Forget", "Forget ROM", "@launcher:forget", 1, FullWidth(kLauncherButtonH));
    RecompButton* mods = SettingButton(b, buttons, "Mods", "Mods", "@launcher:mods", 1, FullWidth(kLauncherButtonH));
    RecompButton* quit = SettingButton(b, buttons, "Quit", "Quit", "@launcher:quit", 1, FullWidth(kLauncherButtonH));
    // the labels are launcher settings, not formats
    for (RecompButton* btn : {play, browse, forget, mods, quit})
    {
        if (btn != nullptr) btn->SetLabelFormat("");
    }
    LinkNavigation({{play}, {browse}, {forget}, {mods}, {quit}});

    RecompMenuController* controller = b.Ensure<RecompMenuController>(root.Get(), "MenuController", [&](RecompMenuController* c) {
        Place(c, 0.0f, 0.0f, 0.0f, 0.0f);
        c->Setup(map->mTitle.empty() ? std::string("Launcher") : map->mTitle + " launcher", true, true, play);
    });
    if (controller != nullptr)
    {
        // always shown, the gamepad moves over its buttons, B doesn't close it
        controller->SetPanel(panel);
        controller->SetInHomeMenu(false);
        controller->SetToggleButton(-1);
        controller->SetCloseOnBack(false);
        root->SetVisible(true);
    }
    RecompLauncher* launcher = b.Ensure<RecompLauncher>(root.Get(), "Launcher", [&](RecompLauncher* n) {
        Place(n, 0.0f, 0.0f, 0.0f, 0.0f);
    });
    // the footer, under the content: bottom left its line with the logo after it, bottom right
    // the version (ModLauncher_ApplyLook sizes and fills it)
    Widget* footer = Array(b, body, "Footer", true, 0.0f, 0.0f, FullWidth(32.0f), true);
    Widget* footerLeft = Array(b, footer, "FooterLeft", true, 6.0f, 0.0f, At(0.0f, 0.0f, 300.0f, 32.0f), true);
    // (a launcher made before: the footer's nodes were on the root)
    for (const char* name : {"FooterText", "FooterLogo"})
    {
        if (Node* old = root->FindChild(name, false))
        {
            if (footerLeft != nullptr) old->Attach(footerLeft);
        }
    }
    b.Ensure<Text>(footerLeft, "FooterText", [&](Text* t) { Place(t, 0.0f, 0.0f, 220.0f, 16.0f); });
    b.Ensure<Quad>(footerLeft, "FooterLogo", [&](Quad* q) { Place(q, 0.0f, 0.0f, 72.0f, 20.0f); });
    b.Ensure<Widget>(footer, "FooterSpace", [&](Widget* w) { FillRest(w); });
    if (Node* old = root->FindChild("FooterVersion", false))
    {
        if (footer != nullptr) old->Attach(footer);
    }
    b.Ensure<RecompText>(footer, "FooterVersion", [&](RecompText* t) {
        Place(t, 0.0f, 0.0f, 240.0f, 16.0f);
        t->SetFormat(l.mVersionFormat);
    });
    std::string modsNote;
    if (launcher != nullptr)
    {
        const std::string modsScene = ModScene_DefaultName(map);
        AssetStub* modsStub = FetchAssetStub(modsScene);
        if (modsStub != nullptr && modsStub->mType == Scene::GetStaticType())
        {
            launcher->SetModsScene(AssetRef(LoadAsset<Scene>(modsScene)));
        }
        else if (l.mShowMods)
        {
            modsNote = " Generate the Mod Settings scene too (the Mods button opens it), then Update this.";
        }
    }

    // the map's look (menu style + launcher settings) over everything generated or kept
    ModLauncher_ApplyLook(root.Get(), *map);

    if (!updating)
    {
        std::string error;
        AssetDir* dir = SceneDir(error);
        if (dir == nullptr)
        {
            outMessage = error;
            return false;
        }
        stub = EditorAddUniqueAsset(sceneName.c_str(), dir, Scene::GetStaticType(), true);
        scene = (stub && stub->mAsset) ? stub->mAsset->As<Scene>() : nullptr;
        if (scene == nullptr)
        {
            outMessage = "Cannot create the Scene asset " + sceneName;
            return false;
        }
    }
    scene->Capture(root.Get());
    AssetManager::Get()->SaveAsset(*stub);
    outMessage = std::string(updating ? "Updated " : "Created ") + stub->mName + ": " + std::to_string(b.added) +
                 " node(s) added" + (updating ? ", " + std::to_string(b.kept) + " kept as they were" : std::string()) +
                 ". Make it the scene the game opens with, and pick the Game Scene in the Launcher window." + modsNote;
    LogDebug("Mods: %s", outMessage.c_str());
    return true;
}

int ModLauncher_RestyleOpen(const ModMap& map)
{
    World* world = GetWorld(0);
    return world != nullptr ? RestyleLaunchers(world->GetRootNode(), map) : 0;
}

bool ModLauncher_ApplyLookToScene(ModMap* map, const std::string& sceneName, std::string& outMessage)
{
    if (map == nullptr)
    {
        outMessage = "No Mod Map selected.";
        return false;
    }
    const std::string name = sceneName.empty() ? ModLauncher_DefaultName(map) : sceneName;
    AssetStub* stub = FetchAssetStub(name);
    Scene* scene = (stub != nullptr && stub->mType == Scene::GetStaticType()) ? LoadAsset<Scene>(name) : nullptr;
    if (scene == nullptr)
    {
        outMessage = "No scene " + name + ": generate it first.";
        return false;
    }
    NodePtr root = scene->Instantiate();
    if (root.Get() == nullptr)
    {
        outMessage = "Cannot open the scene " + name + ".";
        return false;
    }
    ModLauncher_ApplyLook(root.Get(), *map);
    scene->Capture(root.Get());
    AssetManager::Get()->SaveAsset(*stub);
    const int live = ModLauncher_RestyleOpen(*map);
    outMessage = "Updated the look of " + name + (live > 0 ? " and " + std::to_string(live) + " launcher(s) open in the editor." : ".");
    LogDebug("Mods: %s", outMessage.c_str());
    return true;
}


#endif
