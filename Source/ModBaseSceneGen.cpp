/**
 * @file ModBaseSceneGen.cpp
 * @brief Generate / update a Mod Settings scene (see ModBaseSceneGen.h).
 */

#include "ModBaseSceneGen.h"

#if EDITOR

#include "ModBaseModMap.h"
#include "ModBaseSettings.h"
#include "ModBaseUiBuilder.h"

#include "AssetDir.h"
#include "AssetManager.h"
#include "Assets/Scene.h"
#include "Editor/EditorUtils.h"

#include <algorithm>
#include <cctype>

namespace
{
using namespace RecompUi;

const float kColumnW = 380.0f;
const float kRowH = 30.0f;
const float kRowGap = 4.0f;
const float kPad = 14.0f;
const float kPagesY = 82.0f;
const int kMaxRowsPerColumn = 12;

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

// One row; returns its buttons (left to right) for gamepad navigation.
std::vector<Button*> MakeRow(Builder& b, Node* page, const ModEntry& e, float x, float y)
{
    std::vector<Button*> buttons;
    const std::string label = e.mLabel.empty() ? e.mId : e.mLabel;
    const std::string shown = label + (e.mSource == ModSource::StartupOption ? " *" : "");
    Widget* row = Group(b, page, "Row_" + NodeName(e.mId), x, y, kColumnW - 8.0f, kRowH);
    if (row == nullptr)
    {
        return buttons;
    }
    const float w = kColumnW - 8.0f;
    switch (e.mKind)
    {
    case ModKind::Toggle:
    case ModKind::Choice:
        buttons.push_back(SettingButton(b, row, "Button", shown + ": {@" + e.mId + "}", e.mId, 1, 0, 0, w, kRowH));
        break;
    case ModKind::Int:
    case ModKind::Float:
        Label(b, row, "Label", shown, 0, 3, w - 150.0f);
        Bound(b, row, "Value", "{@" + e.mId + "}", w - 146.0f, 3, 60.0f, kFontSize, kHeaderColor);
        buttons.push_back(SettingButton(b, row, "Minus", "-", e.mId, -1, w - 82.0f, 0, 38.0f, kRowH));
        buttons.push_back(SettingButton(b, row, "Plus", "+", e.mId, 1, w - 40.0f, 0, 38.0f, kRowH));
        break;
    case ModKind::Action:
        buttons.push_back(SettingButton(b, row, "Button", shown, e.mId, 1, 0, 0, w, kRowH));
        break;
    case ModKind::Display:
        Label(b, row, "Label", shown, 0, 3, w * 0.45f, kFontSize, kDimColor);
        Bound(b, row, "Value", ValueFormat(e), w * 0.45f, 3, w * 0.55f);
        break;
    case ModKind::Bar:
        Label(b, row, "Label", shown, 0, 3, 140.0f, kFontSize, kDimColor);
        Bar(b, row, "Bar", e.mName, e.mMaxName, 144.0f, 3, w - 144.0f, {0.35f, 0.8f, 0.35f, 1.0f});
        break;
    default:
        break;
    }
    return buttons;
}

AssetDir* SceneDir(const std::string& game, std::string& outError)
{
    AssetManager* am = AssetManager::Get();
    AssetDir* project = am ? am->FindProjectDirectory() : nullptr;
    AssetDir* packages = (project && project->mParentDir) ? project->mParentDir->GetSubdirectory("Packages") : nullptr;
    AssetDir* gameDir = packages ? packages->GetSubdirectory(game) : nullptr;
    if (gameDir == nullptr)
    {
        // a game without a package (or an unknown id): the project's own Assets
        gameDir = project;
        if (gameDir == nullptr)
        {
            outError = "No project is open.";
            return nullptr;
        }
    }
    AssetDir* scenes = gameDir->GetSubdirectory("Scenes");
    if (scenes == nullptr)
    {
        scenes = gameDir->CreateSubdirectory("Scenes");
    }
    if (scenes == nullptr)
    {
        outError = "Cannot create the Scenes folder in " + gameDir->mPath;
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
        for (const char* id : {"display.mode", "display.scale", "display.filter", "display.window"})
        {
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

    // panel size from the largest page
    int columns = 1, rows = 1;
    for (const auto& list : byGroup)
    {
        const int n = (int)list.size();
        const int cols = n > kMaxRowsPerColumn ? 2 : 1;
        columns = std::max(columns, cols);
        rows = std::max(rows, (n + cols - 1) / cols);
    }
    const float panelW = columns * kColumnW + kPad * 2.0f;
    const float panelH = kPagesY + rows * (kRowH + kRowGap) + kRowH + kPad * 2.0f + 22.0f;

    Builder b;
    Quad* panel = b.Ensure<Quad>(root.Get(), "Panel", [&](Quad* q) {
        switch (options.position)
        {
        case 1:
            q->SetAnchorMode(AnchorMode::TopLeft);
            q->SetPosition(16.0f, 16.0f);
            break;
        case 2:
            q->SetAnchorMode(AnchorMode::TopRight);
            q->SetPosition(-panelW - 16.0f, 16.0f);
            break;
        default:
            q->SetAnchorMode(AnchorMode::Mid);
            q->SetPosition(-panelW * 0.5f, -panelH * 0.5f);
            break;
        }
        q->SetDimensions(panelW, panelH);
        q->SetColor(kPanelColor);
    });
    if (panel == nullptr)
    {
        outMessage = "Panel exists but is not a Quad: left as it is.";
        return false;
    }
    Label(b, panel, "Title", map->mTitle.empty() ? std::string("Mod Settings") : map->mTitle, kPad, 8.0f,
          panelW - kPad * 2.0f, 22.0f, kHeaderColor);

    // tabs
    Widget* tabs = Group(b, panel, "Tabs", kPad, 40.0f, panelW - kPad * 2.0f, kRowH);
    const float tabW = std::min(140.0f, (panelW - kPad * 2.0f) / float(groups.size()) - 4.0f);
    std::vector<Button*> tabButtons;
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const std::string group = groups[g].empty() ? std::string("General") : groups[g];
        tabButtons.push_back(SettingButton(b, tabs, "Tab_" + NodeName(group), group, "@page:" + NodeName(group), 1,
                                           float(g) * (tabW + 4.0f), 0.0f, tabW, kRowH));
    }

    // pages
    Widget* pages = Group(b, panel, "Pages", kPad, kPagesY, panelW - kPad * 2.0f, rows * (kRowH + kRowGap));
    std::vector<std::vector<Button*>> pageFirstRows(groups.size());
    for (size_t g = 0; g < groups.size(); ++g)
    {
        const std::string group = groups[g].empty() ? std::string("General") : groups[g];
        Widget* page = b.Ensure<Widget>(pages, "Page_" + NodeName(group), [&](Widget* w) {
            Place(w, 0.0f, 0.0f, panelW - kPad * 2.0f, rows * (kRowH + kRowGap));
            w->SetVisible(g == 0);
        });
        if (page == nullptr)
        {
            continue;
        }
        const auto& list = byGroup[g];
        const int cols = (int)list.size() > kMaxRowsPerColumn ? 2 : 1;
        const int perCol = ((int)list.size() + cols - 1) / cols;
        std::vector<std::vector<std::vector<Button*>>> grid(cols);
        for (size_t i = 0; i < list.size(); ++i)
        {
            const int c = (int)i / perCol, r = (int)i % perCol;
            std::vector<Button*> row = MakeRow(b, page, *list[i], c * kColumnW, r * (kRowH + kRowGap));
            if (!row.empty())
            {
                grid[c].push_back(row);
            }
        }
        // navigation: rows within each column; columns side by side
        for (auto& column : grid)
        {
            LinkNavigation(column);
        }
        if (cols == 2 && !grid[0].empty() && !grid[1].empty())
        {
            for (size_t r = 0; r < grid[0].size() && r < grid[1].size(); ++r)
            {
                Button* left = grid[0][r].back();
                Button* right = grid[1][r].front();
                if (left->GetNavRight() == nullptr) left->SetNavRight(right);
                if (right->GetNavLeft() == nullptr) right->SetNavLeft(left);
            }
        }
        if (!grid[0].empty())
        {
            pageFirstRows[g] = grid[0].front();
            // tab -> its page's first row and back
            if (tabButtons[g] != nullptr && tabButtons[g]->GetNavDown() == nullptr)
            {
                tabButtons[g]->SetNavDown(grid[0].front().front());
            }
            for (Button* btn : grid[0].front())
            {
                if (btn->GetNavUp() == nullptr) btn->SetNavUp(tabButtons[g]);
            }
        }
    }
    LinkNavigation({tabButtons});

    // footer
    const float footerY = panelH - kPad - kRowH - 20.0f;
    Widget* footer = Group(b, panel, "Footer", kPad, footerY, panelW - kPad * 2.0f, kRowH + 20.0f);
    Button* save = SettingButton(b, footer, "Save", "Save", "@save", 1, 0.0f, 0.0f, 110.0f, kRowH);
    Button* reset = SettingButton(b, footer, "Reset", "Reset to defaults", "@reset", 1, 116.0f, 0.0f, 170.0f, kRowH);
    Button* close = SettingButton(b, footer, "Close", "Close", "@close", 1, 292.0f, 0.0f, 110.0f, kRowH);
    LinkNavigation({{save, reset, close}});
    // pages -> footer (each page's last rows), footer -> tabs
    for (uint32_t p = 0; p < pages->GetNumChildren(); ++p)
    {
        Node* page = pages->GetChild((int32_t)p);
        for (uint32_t r = 0; r < page->GetNumChildren(); ++r)
        {
            Node* row = page->GetChild((int32_t)r);
            for (uint32_t i = 0; i < row->GetNumChildren(); ++i)
            {
                Button* btn = row->GetChild((int32_t)i)->As<Button>();
                if (btn != nullptr && btn->GetNavDown() == nullptr && save != nullptr)
                {
                    btn->SetNavDown(save);
                }
            }
        }
    }
    for (Button* btn : {save, reset, close})
    {
        if (btn != nullptr && btn->GetNavUp() == nullptr && !tabButtons.empty())
        {
            btn->SetNavUp(tabButtons.front());
        }
    }
    for (Button* tab : tabButtons)
    {
        if (tab != nullptr && tab->GetNavDown() == nullptr && save != nullptr)
        {
            tab->SetNavDown(save); // a page without buttons
        }
    }
    bool anyStartup = false;
    for (const ModEntry& e : map->mEntries)
    {
        anyStartup = anyStartup || e.mSource == ModSource::StartupOption;
    }
    if (anyStartup)
    {
        Label(b, footer, "Note", "* applies when the game restarts", 0.0f, kRowH, 400.0f, 13.0f, kDimColor);
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
        controller->SetToggleButton(options.toggleButton);
        controller->SetToggleAction(options.toggleAction);
    }

    // rows of entries no longer in the map: reported, not deleted (they may be the user's)
    std::vector<std::string> stale;
    for (uint32_t p = 0; p < pages->GetNumChildren(); ++p)
    {
        Node* page = pages->GetChild((int32_t)p);
        for (uint32_t r = 0; r < page->GetNumChildren(); ++r)
        {
            const std::string& name = page->GetChild((int32_t)r)->GetName();
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
        AssetDir* dir = SceneDir(map->mGame, error);
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
                 ". Instance it in your game's scene.";
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

#endif
