/**
 * @file ModBaseLauncher.cpp
 * @brief Game launchers, the RecompLauncher node, its tokens and button commands (see
 *        ModBaseLauncher.h).
 */

#include "ModBaseLauncher.h"
#include "ModBaseSound.h"

#include "ModBaseModMap.h"
#include "ModBaseSceneGen.h"
#include "ModBaseSettings.h"
#include "ModBaseUtil.h"
#include "ModBaseWidgets.h"

#include "AssetManager.h"
#include "Assets/Font.h"
#include "Assets/Scene.h"
#include "Assets/SoundWave.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "EngineTypes.h"
#include "Log.h"
#include "Nodes/Widgets/Quad.h"
#include "Nodes/Widgets/ScrollContainer.h"
#include "Nodes/Widgets/Text.h"
#include "System/System.h"
#include "World.h"

#include <algorithm>
#include <cmath>

FORCE_LINK_DEF(RecompLauncher);
DEFINE_NODE(RecompLauncher, Widget);

namespace
{
// Function-local statics: valid whichever addon registers first.
std::vector<RecompGameLauncher*>& Registry()
{
    static std::vector<RecompGameLauncher*> sLaunchers;
    return sLaunchers;
}

std::vector<RecompLauncher*>& Nodes()
{
    static std::vector<RecompLauncher*> sNodes;
    return sNodes;
}

bool IsInside(Node* node, Node* ancestor)
{
    for (Node* n = node; n != nullptr; n = n->GetParent())
    {
        if (n == ancestor) return true;
    }
    return false;
}
}

// ---- registry -------------------------------------------------------------------------------
void Recomp_RegisterLauncher(RecompGameLauncher* launcher)
{
    std::vector<RecompGameLauncher*>& all = Registry();
    if (launcher != nullptr && std::find(all.begin(), all.end(), launcher) == all.end())
    {
        all.push_back(launcher);
    }
}

void Recomp_UnregisterLauncher(RecompGameLauncher* launcher)
{
    std::vector<RecompGameLauncher*>& all = Registry();
    all.erase(std::remove(all.begin(), all.end(), launcher), all.end());
}

RecompGameLauncher* Recomp_FindLauncher(const char* gamePackage)
{
    const std::vector<RecompGameLauncher*>& all = Registry();
    if (gamePackage != nullptr && gamePackage[0] != 0)
    {
        for (RecompGameLauncher* l : all)
        {
            if (l->GamePackage() == gamePackage) return l;
        }
        return nullptr;
    }
    return all.empty() ? nullptr : all.front();
}

const std::vector<RecompGameLauncher*>& Recomp_Launchers()
{
    return Registry();
}

std::string Recomp_BrowseForFile()
{
#if defined(_WIN32) || defined(__linux__)
    const std::vector<std::string> picked = SYS_OpenFileDialog();
    return picked.empty() ? std::string() : picked[0];
#else
    return std::string();
#endif
}

bool Recomp_LoadMods(const std::string& gamePackage)
{
    return !gamePackage.empty() && ModSettings::Get().Bind(gamePackage) != nullptr;
}

// ---- tokens and commands --------------------------------------------------------------------
bool RecompLauncher_Token(const std::string& name, std::string& out)
{
    if (name == "version" || name == "project")
    {
        const EngineConfig* config = GetEngineConfig();
        out = config == nullptr ? std::string(" ")
              : name == "version" ? std::to_string(config->mVersion)
                                  : (config->mProjectName.empty() ? std::string(" ") : config->mProjectName);
        return true;
    }
    RecompLauncher* node = RecompLauncher::Find();
    RecompGameLauncher* game = node != nullptr ? node->Game() : Recomp_FindLauncher();
    if (game == nullptr)
    {
        if (name == "romfile") out = "No ROM chosen";
        else if (name == "status") out = "idle";
        else if (name == "ready") out = "0";
        else if (name == "message" && node != nullptr) out = node->GetMessage();
        else if (name != "title" && name != "rom" && name != "message") return false;
        if (out.empty()) out = " ";
        return true;
    }
    if (name == "title") out = game->GameTitle();
    else if (name == "rom") out = game->GetRomLocation();
    else if (name == "romfile")
    {
        const std::string rom = game->GetRomLocation();
        out = rom.empty() ? std::string(game->HasShippedData() ? "Game data included" : "No ROM chosen") : RecompUtil::FileName(rom);
    }
    else if (name == "message") out = node != nullptr ? node->GetMessage() : game->LastMessage();
    else if (name == "status") out = node != nullptr ? node->GetStatus() : (game->IsStarted() ? "running" : "idle");
    else if (name == "ready") out = (node != nullptr ? node->IsReady() : !game->GetRomLocation().empty()) ? "1" : "0";
    else return false;
    // an empty message is a real value (nothing to say), not a missing one
    if (out.empty()) out = " ";
    return true;
}

namespace
{
bool& BackFromGame()
{
    static bool sBack = false; // set by Main Menu, taken by the next launcher's auto start
    return sBack;
}
}

bool Recomp_GoToMainMenu(Node* from)
{
    World* world = from != nullptr ? from->GetWorld() : GetWorld(0);
    if (world == nullptr)
    {
        return false;
    }
    std::string sceneName;
    if (RecompMenuController* controller = RecompMenuController::FindFor(from))
    {
        if (Scene* scene = controller->GetMainMenuScene().Get<Scene>()) sceneName = scene->GetName();
    }
    if (sceneName.empty())
    {
        const EngineConfig* config = GetEngineConfig();
        if (config != nullptr) sceneName = config->mDefaultScene;
    }
    if (sceneName.empty())
    {
        LogWarning("Main Menu: no scene to go to (set the menu's Main Menu Scene, or the project's default scene)");
        return false;
    }
    BackFromGame() = true;
    LogDebug("Main Menu: %s", sceneName.c_str());
    world->LoadScene(sceneName.c_str(), false);
    return true;
}

bool RecompLauncher_Command(const std::string& command, Node* from)
{
    RecompLauncher* launcher = RecompLauncher::Find(from);
    if (launcher == nullptr)
    {
        LogWarning("@launcher:%s: no RecompLauncher node in the running scene", command.c_str());
        return false;
    }
    if (command == "play") launcher->Play();
    else if (command == "browse") launcher->Browse();
    else if (command == "forget") launcher->ForgetRom();
    else if (command == "mods") launcher->OpenMods();
    else if (command == "quit") launcher->Quit();
    else
    {
        LogWarning("@launcher:%s: unknown command (play, browse, forget, mods, quit)", command.c_str());
        return false;
    }
    return true;
}

// ---- look -----------------------------------------------------------------------------------
namespace
{
void StyleText(Node* root, const char* name, Font* font, float size, glm::vec4 color, const std::string* text)
{
    Node* node = root->FindChild(name, true);
    Text* t = node != nullptr ? node->As<Text>() : nullptr;
    if (t == nullptr) return;
    t->SetFont(font);
    t->SetTextSize(size);
    t->SetColor(color);
    if (text != nullptr) t->SetText(*text);
}

float FloatProperty(Node* node, const char* name, float fallback)
{
    std::vector<Property> props;
    node->GatherProperties(props);
    for (Property& p : props)
    {
        if (p.mName == name && p.GetType() == DatumType::Float) return p.GetFloat();
    }
    return fallback;
}

// An ArrayWidget column as tall as its children (it doesn't size itself); the height. Counted
// the way ArrayWidget lays them out: hidden children keep their place too.
float FitColumn(Widget* column)
{
    const float spacing = FloatProperty(column, "Spacing", 0.0f);
    float total = FloatProperty(column, "Padding Top", 0.0f) + FloatProperty(column, "Padding Bottom", 0.0f);
    int count = 0;
    for (uint32_t i = 0; i < column->GetNumChildren(); ++i)
    {
        Widget* w = column->GetChild((int32_t)i)->As<Widget>();
        if (w == nullptr) continue;
        total += w->GetHeight();
        ++count;
    }
    if (count > 1) total += spacing * float(count - 1);
    column->SetHeight(total);
    return total;
}

// An ArrayWidget's padding: left and right `x`, top and bottom `y` (properties: ArrayWidget isn't
// exported to addons).
void SetPadding(Widget* array, float x, float y)
{
    std::vector<Property> props;
    array->GatherProperties(props);
    for (Property& p : props)
    {
        if (p.GetType() != DatumType::Float) continue;
        if (p.mName == "Padding Left" || p.mName == "Padding Right") p.SetFloat(x);
        else if (p.mName == "Padding Top" || p.mName == "Padding Bottom") p.SetFloat(y);
    }
}

constexpr float kLauncherButtonH = 34.0f; // as ModLauncher_Generate makes them

// a hidden button takes no room (a column keeps a hidden child's place)
void ShowButton(RecompButton* b, bool shown)
{
    b->SetVisible(shown);
    b->SetHeight(shown ? kLauncherButtonH : 0.0f);
}

void LabelButton(Node* root, const char* name, const std::string& label, bool shown)
{
    Node* node = root->FindChild(name, true);
    RecompButton* b = node != nullptr ? node->As<RecompButton>() : nullptr;
    if (b == nullptr) return;
    b->SetTextString(label);
    ShowButton(b, shown);
}

// Up / down over the buttons that are shown (a hidden one would stop the gamepad), and the
// content's height: the scroll view around it scrolls when the window is shorter, so the
// buttons can always be reached.
void RelayoutButtons(Node* root)
{
    std::vector<Button*> shown;
    for (const char* name : {"Play", "Browse", "Forget", "Mods", "Quit"})
    {
        Node* node = root->FindChild(name, true);
        RecompButton* b = node != nullptr ? node->As<RecompButton>() : nullptr;
        if (b != nullptr && b->IsVisible()) shown.push_back(b);
    }
    for (size_t i = 0; i < shown.size(); ++i)
    {
        shown[i]->SetNavUp(i > 0 ? shown[i - 1] : nullptr);
        shown[i]->SetNavDown(i + 1 < shown.size() ? shown[i + 1] : nullptr);
    }
    if (Node* node = root->FindChild("Buttons", true))
    {
        if (Widget* w = node->As<Widget>()) FitColumn(w);
    }
    if (Node* node = root->FindChild("Layout", true))
    {
        Widget* layout = node->As<Widget>();
        if (layout != nullptr && node->GetParent() != nullptr && node->GetParent()->As<ScrollContainer>() != nullptr)
        {
            // top-left in the scroll view; its width follows the view (FitWidth)
            layout->SetAnchorMode(AnchorMode::TopLeft);
            layout->SetPosition(0.0f, 0.0f);
            FitColumn(layout);
        }
    }
}
}

void ModLauncher_ApplyLook(Node* root, const ModMap& map)
{
    if (root == nullptr)
    {
        return;
    }
    const ModStyle& s = map.mStyle;
    const ModLauncherSettings& l = map.mLauncher;
    ModStyle_Apply(root, s); // panel, buttons, the title's size and color

    Font* header = ModStyle_Font(s, s.mHeaderFont);
    Font* body = ModStyle_Font(s, s.mBodyFont);
    const std::string title = !l.mTitle.empty() ? l.mTitle : (!map.mTitle.empty() ? map.mTitle : std::string("Game"));
    StyleText(root, "Title", header, s.mTitleSize * 1.4f, s.mTitleColor, &title);
    StyleText(root, "Subtitle", body, s.mLabelSize, s.mInfoColor, &l.mSubtitle);
    if (Node* sub = root->FindChild("Subtitle", true))
    {
        sub->SetVisible(!l.mSubtitle.empty());
        if (Widget* w = sub->As<Widget>()) w->SetHeight(l.mSubtitle.empty() ? 0.0f : 22.0f);
    }
    // the ROM and message lines fill in as the launcher runs; until then (the editor) they show
    // what a first start says, not their tokens
    RecompGameLauncher* game = Recomp_FindLauncher(map.mGame.empty() ? nullptr : map.mGame.c_str());
    const std::string romPreview = RecompFormat("{@launcher.romfile}");
    const std::string messagePreview =
        game == nullptr ? std::string(" ")
        : !game->GetRomLocation().empty() || game->HasShippedData() ? std::string("Ready")
                                                                    : "Choose your ROM of " + game->GameTitle() + " to play";
    StyleText(root, "Rom", body, s.mValueSize, s.mValueColor, &romPreview);
    StyleText(root, "Message", body, s.mNoteSize, s.mInfoColor, &messagePreview);

    if (Node* node = root->FindChild("Background", false))
    {
        if (Quad* q = node->As<Quad>())
        {
            Texture* picture = l.mBackground.Get<Texture>();
            // a picture untinted shows as it is; without one the color is the fill
            q->SetColor(picture != nullptr && !l.mTintBackground ? glm::vec4(1.0f) : l.mBackgroundColor);
            q->SetTexture(picture);
            q->SetVisible(l.mShowBackground); // off: the scene behind shows (a 3D background)
        }
    }
    if (Node* node = root->FindChild("Panel", false))
    {
        // full screen: stretched over the whole UI, as the editor shows it (the controller
        // doesn't fit it then)
        Widget* panel = node->As<Widget>();
        if (panel != nullptr && l.mPanelFullScreen)
        {
            panel->SetAnchorMode(AnchorMode::FullStretch);
            panel->SetRatios(0.0f, 0.0f, 1.0f, 1.0f);
        }
    }
    if (Node* node = root->FindChild("Logo", true))
    {
        if (Quad* q = node->As<Quad>())
        {
            Texture* logo = l.mLogo.Get<Texture>();
            q->SetTexture(logo);
            q->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
            q->SetUvScale(l.mLogoUvScale);
            q->SetUvOffset(l.mLogoUvOffset);
            q->SetObjectFit(l.mLogoFit < uint8_t(ObjectFit::Count) ? ObjectFit(l.mLogoFit) : ObjectFit::Contain);
            q->SetDimensions(l.mLogoSize.x, l.mLogoSize.y);
            q->SetVisible(logo != nullptr);
        }
    }
    // the footer (Panel/Body/Footer, under the scrolling content): bottom left its line with the
    // logo right after it (FooterLeft, a row), a spacer, bottom right the version
    if (Node* footerNode = root->FindChild("Footer", true))
    {
        Widget* footer = footerNode->As<Widget>();
        const float margin = 12.0f;
        const float textSize = l.mFooterTextSize > 0.0f ? l.mFooterTextSize : 5.0f;
        const float lineH = textSize + 3.0f;
        Texture* logo = l.mFooterLogo.Get<Texture>();
        const bool hasText = !l.mFooterText.empty();
        const bool hasLogo = logo != nullptr;
        const bool hasVersion = !l.mVersionFormat.empty();
        const bool shown = l.mShowFooter && (hasText || hasLogo || hasVersion);
        const float rowH = shown ? std::max(lineH, hasLogo ? l.mFooterLogoSize.y : 0.0f) + 2.0f * 2.0f : 0.0f;
        if (footer != nullptr)
        {
            footer->SetVisible(shown);
            footer->SetHeight(rowH);
            SetPadding(footer, margin, 0.0f);
        }
        float textW = 0.0f;
        if (Node* node = footerNode->FindChild("FooterText", true))
        {
            if (Text* t = node->As<Text>())
            {
                t->SetFont(body);
                t->SetTextSize(textSize);
                t->SetColor(s.mInfoColor);
                t->SetText(l.mFooterText);
                t->SetVerticalJustification(Justification::Center);
                // its width in layout pixels (GetTextWidth's are: font units x size / font size)
                textW = hasText ? t->GetTextWidth() : 0.0f;
                if (hasText && textW <= 0.0f) textW = float(l.mFooterText.size()) * textSize * 0.55f;
                if (hasText) textW += 2.0f;
                t->SetAnchorMode(AnchorMode::TopLeft);
                t->SetDimensions(textW, lineH);
                t->SetVisible(hasText);
            }
        }
        if (Node* node = footerNode->FindChild("FooterLogo", true))
        {
            if (Quad* q = node->As<Quad>())
            {
                q->SetTexture(logo);
                q->SetColor({1.0f, 1.0f, 1.0f, 1.0f});
                q->SetObjectFit(ObjectFit::Contain);
                q->SetAnchorMode(AnchorMode::TopLeft);
                q->SetDimensions(hasLogo ? l.mFooterLogoSize.x : 0.0f, hasLogo ? l.mFooterLogoSize.y : 0.0f);
                q->SetVisible(hasLogo);
            }
        }
        if (Node* node = footerNode->FindChild("FooterLeft", true))
        {
            if (Widget* left = node->As<Widget>())
            {
                const float logoW = hasLogo ? l.mFooterLogoSize.x : 0.0f;
                left->SetAnchorMode(AnchorMode::TopLeft);
                left->SetDimensions(textW + (hasText && hasLogo ? 6.0f : 0.0f) + logoW, rowH);
            }
        }
        if (Node* node = footerNode->FindChild("FooterVersion", true))
        {
            if (RecompText* t = node->As<RecompText>())
            {
                t->SetFont(body);
                t->SetTextSize(textSize);
                t->SetColor(s.mInfoColor);
                t->SetFormat(l.mVersionFormat);
                t->SetText(RecompFormat(l.mVersionFormat));
                t->SetHorizontalJustification(Justification::Right);
                t->SetVerticalJustification(Justification::Center);
                t->SetAnchorMode(AnchorMode::TopLeft);
                t->SetDimensions(hasVersion ? 240.0f : 0.0f, lineH);
                t->SetVisible(hasVersion);
            }
        }
    }

    LabelButton(root, "Play", l.mPlayLabel, true);
    LabelButton(root, "Browse", l.mBrowseLabel, true);
    LabelButton(root, "Forget", l.mForgetLabel, l.mShowForget);
    LabelButton(root, "Mods", l.mModsLabel, l.mShowMods);
    LabelButton(root, "Quit", l.mQuitLabel, l.mShowQuit);
    // Forget ROM only while a ROM is set (the launcher keeps it so as it runs)
    if (l.mShowForget)
    {
        Node* node = root->FindChild("Forget", true);
        RecompButton* b = node != nullptr ? node->As<RecompButton>() : nullptr;
        if (b != nullptr) ShowButton(b, game != nullptr && !game->GetRomLocation().empty());
    }
    RelayoutButtons(root);

    for (uint32_t i = 0; i < root->GetNumChildren(); ++i)
    {
        Node* child = root->GetChild((int32_t)i);
        if (RecompLauncher* launcher = child->As<RecompLauncher>())
        {
            launcher->SetGame(map.mGame);
            launcher->SetGameScene(l.mGameScene);
            launcher->SetAutoStart(l.mAutoStart);
            launcher->SetSounds(l.mSoundStart, l.mSoundQuit, l.mMusic, l.mMusicVolume);
#if EDITOR
            // the Mods button's scene: the settings scene, made before or after the launcher
            const std::string modsScene = ModScene_DefaultName(&map);
            AssetStub* modsStub = FetchAssetStub(modsScene);
            if (modsStub != nullptr && modsStub->mType == Scene::GetStaticType())
            {
                launcher->SetModsScene(AssetRef(LoadAsset<Scene>(modsScene)));
            }
#endif
            launcher->SetMoreSounds(l.mSoundDeny, l.mSoundForget);
            launcher->SetCenterContent(l.mCenterContent);
            launcher->SetShowForget(l.mShowForget);
        }
        else if (RecompMenuController* controller = child->As<RecompMenuController>())
        {
            controller->SetPanelFit(l.mPanelFullScreen ? glm::vec2(0.0f) : l.mPanelSize, 16.0f, l.mPosition);
        }
    }
}

// ---- RecompLauncher -------------------------------------------------------------------------
RecompLauncher* RecompLauncher::Find(Node* from)
{
    const std::vector<RecompLauncher*>& all = Nodes();
    if (from != nullptr)
    {
        // the launcher whose UI (its parent's tree) holds `from`
        for (RecompLauncher* l : all)
        {
            Node* root = l->GetParent();
            if (root != nullptr && IsInside(from, root)) return l;
        }
    }
    return all.empty() ? nullptr : all.front();
}

void RecompLauncher::Start()
{
    Widget::Start();
    std::vector<RecompLauncher*>& all = Nodes();
    if (std::find(all.begin(), all.end(), this) == all.end())
    {
        all.push_back(this);
    }
    mSetUp = false;
    mStartCountdown = 0;
    mFailed = false;
}

void RecompLauncher::Stop()
{
    std::vector<RecompLauncher*>& all = Nodes();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
    Widget::Stop();
}

void RecompLauncher::Destroy()
{
    std::vector<RecompLauncher*>& all = Nodes();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
    Widget::Destroy();
}

RecompGameLauncher* RecompLauncher::Game() const
{
    return Recomp_FindLauncher(mGame.c_str());
}

const std::string& RecompLauncher::GetMessage() const
{
    return mMessage;
}

const char* RecompLauncher::GetStatus() const
{
    if (mStartCountdown > 0) return "starting";
    RecompGameLauncher* game = Game();
    if (game != nullptr && game->IsStarted()) return "running";
    return mFailed ? "failed" : "idle";
}

bool RecompLauncher::IsReady() const
{
    RecompGameLauncher* game = Game();
    return game != nullptr && (!game->GetRomLocation().empty() || game->HasShippedData());
}

void RecompLauncher::SetMessage(const std::string& message)
{
    mMessage = message;
    if (!message.empty())
    {
        LogDebug("Launcher: %s", message.c_str());
    }
}

// What the launcher says before anything happened: the ROM it will start, or what it needs.
void RecompLauncher::DescribeRom()
{
    RecompGameLauncher* game = Game();
    if (game == nullptr)
    {
        SetMessage(mGame.empty() ? std::string("No game to launch: its runtime registers none")
                                 : "No game " + mGame + " to launch (is its addon loaded?)");
        return;
    }
    const std::string rom = game->GetRomLocation();
    std::string message;
    if (!rom.empty())
    {
        game->CheckRom(rom, message);
    }
    else if (game->HasShippedData())
    {
        message = "Ready";
    }
    else
    {
        message = "Choose your ROM of " + game->GameTitle() + " to play";
    }
    SetMessage(message);
}

// The mod settings menu, from Mods Scene when the scene has none of its own.
void RecompLauncher::AddModsScene()
{
    Scene* scene = mModsScene.Get<Scene>();
    Node* root = GetParent();
    if (scene == nullptr || root == nullptr || RecompMenuController::FindSettingsMenu() != nullptr)
    {
        return;
    }
    NodePtr mods = scene->Instantiate();
    if (mods.Get() != nullptr)
    {
        root->AddChild(mods.Get());
    }
}

void RecompLauncher::Tick(float deltaTime)
{
    Widget::Tick(deltaTime);
    if (!mSetUp)
    {
        mSetUp = true;
        RecompGameLauncher* game = Game();
        if (game != nullptr && mGame.empty())
        {
            mGame = game->GamePackage();
        }
        if (mLoadMods && game != nullptr)
        {
            Recomp_LoadMods(game->GamePackage());
        }
        AddModsScene();
        DescribeRom();
        if (!mMusicStarted)
        {
            mMusicStarted = true;
            RecompSound::Play(this, mMusic.Get<SoundWave>(), mMusicVolume, true, "music");
        }
        // (back from the game through Main Menu: the player wants the menu, not the game again)
        const bool backFromGame = BackFromGame();
        BackFromGame() = false;
        if (mAutoStart && !backFromGame && game != nullptr && !game->IsStarted() && IsReady())
        {
            Play();
        }
    }
    RecompSound::Follow(this);
    if (mOpenModsPending > 0 && --mOpenModsPending == 0)
    {
        if (RecompMenuController* menu = RecompMenuController::FindSettingsMenu()) menu->Open();
        else SetMessage("The Mods Scene has no settings menu (its MenuController): generate it again");
    }
    if ((mForgetCheck -= deltaTime) <= 0.0f) UpdateForgetButton();
    CenterContent();
    // Play shows "Starting..." for a frame first (the start may take a moment), and lets its
    // sound play out: the game scene replaces this one, sounds and all
    if (mStartCountdown > 0)
    {
        if (mStartCountdown > 1) --mStartCountdown;
        else if ((mStartWait -= deltaTime) <= 0.0f)
        {
            mStartCountdown = 0;
            StartNow();
        }
    }
#if !EDITOR
    if (mQuitWait >= 0.0f && (mQuitWait -= deltaTime) < 0.0f)
    {
        GetEngineState()->mQuit = true;
    }
#endif
}

void RecompLauncher::SetCenterContent(bool center)
{
    mCenterContent = center;
}

void RecompLauncher::EditorTick(float deltaTime)
{
    Widget::EditorTick(deltaTime);
    // the editor's preview as the game shows it
    if ((mForgetCheck -= deltaTime) <= 0.0f) UpdateForgetButton();
    CenterContent();
}

void RecompLauncher::SetShowForget(bool show)
{
    mShowForget = show;
    mForgetCheck = 0.0f;
}

// Forget ROM shows while there is a ROM to forget (checked now and then: a script may set one).
void RecompLauncher::UpdateForgetButton()
{
    mForgetCheck = 0.5f;
    Node* root = GetParent();
    Node* node = root != nullptr ? root->FindChild("Forget", true) : nullptr;
    RecompButton* forget = node != nullptr ? node->As<RecompButton>() : nullptr;
    if (forget == nullptr)
    {
        return;
    }
    RecompGameLauncher* game = Game();
    const bool shown = mShowForget && game != nullptr && !game->GetRomLocation().empty();
    if (forget->IsVisible() != shown)
    {
        ShowButton(forget, shown);
        RelayoutButtons(root);
    }
}

// The launcher's content column (a scroll view's content) has a top padding of kLayoutPadding;
// where the view is taller than the content, the padding grows by half of what is spare, which
// puts the content in the middle. Shorter, it is back to kLayoutPadding and the view scrolls.
void RecompLauncher::CenterContent()
{
    constexpr float kLayoutPadding = 16.0f;
    Node* root = GetParent();
    Node* node = root != nullptr ? root->FindChild("Layout", true) : nullptr;
    Widget* layout = node != nullptr ? node->As<Widget>() : nullptr;
    ScrollContainer* view = layout != nullptr && layout->GetParent() != nullptr
                                ? layout->GetParent()->As<ScrollContainer>() : nullptr;
    if (view == nullptr)
    {
        return;
    }
    const glm::vec2 scale = view->GetAbsoluteScale();
    const float viewH = scale.y > 0.0f ? view->GetRect().mHeight / scale.y : 0.0f;
    if (viewH <= 0.0f)
    {
        return;
    }
    const float padTop = FloatProperty(layout, "Padding Top", kLayoutPadding);
    const float extraNow = std::max(0.0f, padTop - kLayoutPadding);
    const float contentH = layout->GetHeight() - extraNow; // the column without the centring
    const float extra = mCenterContent ? std::floor(std::max(0.0f, (viewH - contentH) * 0.5f)) : 0.0f;
    if (std::fabs(extra - extraNow) < 0.5f)
    {
        return;
    }
    std::vector<Property> props;
    layout->GatherProperties(props);
    for (Property& p : props)
    {
        if (p.mName == "Padding Top" && p.GetType() == DatumType::Float) p.SetFloat(kLayoutPadding + extra);
    }
    layout->SetHeight(contentH + extra);
}

void RecompLauncher::SetMoreSounds(const AssetRef& deny, const AssetRef& forget)
{
    mSoundDeny = deny;
    mSoundForget = forget;
}

void RecompLauncher::SetSounds(const AssetRef& start, const AssetRef& quit, const AssetRef& music, float musicVolume)
{
    mSoundStart = start;
    mSoundQuit = quit;
    mMusic = music;
    mMusicVolume = musicVolume;
}

namespace
{
// a launcher sound at its menu's effects volume (the menu's Select sound when it has none,
// unless `selectIfNone` is off)
float PlayLauncherSound(RecompLauncher* launcher, SoundWave* wave, bool selectIfNone = true)
{
    RecompMenuController* menu = RecompMenuController::FindFor(launcher);
    if (wave == nullptr)
    {
        if (menu != nullptr && selectIfNone) menu->PlaySound(RecompMenuController::Sound::Select);
        return 0.0f;
    }
    if (menu != nullptr) menu->PlaySoundWave(wave);
    else RecompSound::Play(launcher, wave, 1.0f);
    return RecompSound::Duration(wave);
}
}

void RecompLauncher::Play()
{
    RecompGameLauncher* game = Game();
    if (game == nullptr)
    {
        PlayLauncherSound(this, mSoundDeny.Get<SoundWave>(), false);
        DescribeRom();
        return;
    }
    if (!IsReady())
    {
        PlayLauncherSound(this, mSoundDeny.Get<SoundWave>(), false);
        SetMessage("Choose your ROM of " + game->GameTitle() + " first");
        return;
    }
    if (mStartCountdown == 0)
    {
        SetMessage("Starting " + game->GameTitle() + "...");
        mStartCountdown = 2;
        mStartWait = std::min(PlayLauncherSound(this, mSoundStart.Get<SoundWave>()), 2.0f);
    }
}

void RecompLauncher::StartNow()
{
    RecompGameLauncher* game = Game();
    if (game == nullptr)
    {
        return;
    }
    // what the player set up goes to the game as it starts
    if (ModSettings::Get().GetMap() != nullptr)
    {
        ModSettings::Get().Save();
    }
    std::string message;
    const bool ok = game->StartGame(message);
    mFailed = !ok;
    SetMessage(message);
    if (!ok)
    {
        PlayLauncherSound(this, mSoundDeny.Get<SoundWave>(), false);
        EmitSignal("GameStartFailed", {this, message});
        CallFunction("OnGameStartFailed", {this, message});
        return;
    }
    RecompSound::Stop(this, "music");
    EmitSignal("GameStarted", {this});
    CallFunction("OnGameStarted", {this});
    Scene* next = mGameScene.Get<Scene>();
    World* world = GetWorld();
    if (next != nullptr && world != nullptr)
    {
        world->LoadScene(next->GetName().c_str(), false);
    }
    else if (RecompMenuController* controller = RecompMenuController::FindFor(this))
    {
        // no scene to go to: the launcher steps aside so the game in this scene gets the input
        controller->Close();
    }
}

void RecompLauncher::Browse()
{
    RecompGameLauncher* game = Game();
    if (game == nullptr)
    {
        DescribeRom();
        return;
    }
    const std::string path = Recomp_BrowseForFile();
    if (path.empty())
    {
        return;
    }
    std::string message;
    const bool ok = game->SetRomLocation(path, message);
    if (!ok) PlayLauncherSound(this, mSoundDeny.Get<SoundWave>(), false);
    SetMessage(message);
    UpdateForgetButton();
    EmitSignal("RomChosen", {this, path, ok});
    CallFunction("OnRomChosen", {this, path, ok});
}

void RecompLauncher::ForgetRom()
{
    PlayLauncherSound(this, mSoundForget.Get<SoundWave>());
    if (RecompGameLauncher* game = Game())
    {
        game->ClearRomLocation();
    }
    DescribeRom();
    UpdateForgetButton();
}

void RecompLauncher::OpenMods()
{
    RecompMenuController* menu = RecompMenuController::FindSettingsMenu();
    if (menu != nullptr)
    {
        menu->Open();
        return;
    }
    if (mModsScene.Get<Scene>() == nullptr)
    {
        SetMessage("No Mods Scene: generate the Mod Settings scene, then Update the launcher");
        return;
    }
    // not added yet: added now, opened once it has started (it hides itself as it starts)
    AddModsScene();
    mOpenModsPending = 3;
}

void RecompLauncher::Quit()
{
    const float sound = std::min(PlayLauncherSound(this, mSoundQuit.Get<SoundWave>()), 1.5f);
#if EDITOR
    (void)sound;
    SetMessage("Quit (closes packaged games; ignored in the editor)");
#else
    // closes once its sound has played
    if (mQuitWait < 0.0f) mQuitWait = sound;
#endif
}

void RecompLauncher::SetGame(const std::string& gamePackage)
{
    mGame = gamePackage;
}

void RecompLauncher::SetGameScene(const AssetRef& scene)
{
    mGameScene = scene;
}

void RecompLauncher::SetModsScene(const AssetRef& scene)
{
    mModsScene = scene;
}

void RecompLauncher::SetAutoStart(bool autoStart)
{
    mAutoStart = autoStart;
}

void RecompLauncher::GatherProperties(std::vector<Property>& outProps)
{
    Widget::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp Launcher");
    outProps.push_back(Property(DatumType::String, "Game", this, &mGame));
    outProps.push_back(Property(DatumType::Asset, "Game Scene", this, &mGameScene, 1, nullptr, int32_t(Scene::GetStaticType())));
    outProps.push_back(Property(DatumType::Asset, "Mods Scene", this, &mModsScene, 1, nullptr, int32_t(Scene::GetStaticType())));
    outProps.push_back(Property(DatumType::Bool, "Auto Start", this, &mAutoStart));
    outProps.push_back(Property(DatumType::Bool, "Load Mods", this, &mLoadMods));
    outProps.push_back(Property(DatumType::Bool, "Center Content", this, &mCenterContent));
    outProps.push_back(Property(DatumType::Bool, "Show Forget", this, &mShowForget));
    {
    SCOPED_CATEGORY("Recomp Launcher Sounds");
    outProps.push_back(Property(DatumType::Asset, "Start Sound", this, &mSoundStart, 1, nullptr,
                                int32_t(SoundWave::GetStaticType())));
    outProps.push_back(Property(DatumType::Asset, "Quit Sound", this, &mSoundQuit, 1, nullptr,
                                int32_t(SoundWave::GetStaticType())));
    outProps.push_back(Property(DatumType::Asset, "Music", this, &mMusic, 1, nullptr,
                                int32_t(SoundWave::GetStaticType())));
    outProps.push_back(Property(DatumType::Float, "Music Volume", this, &mMusicVolume));
    outProps.push_back(Property(DatumType::Asset, "Deny Sound", this, &mSoundDeny, 1, nullptr,
                                int32_t(SoundWave::GetStaticType())));
    outProps.push_back(Property(DatumType::Asset, "Forget Sound", this, &mSoundForget, 1, nullptr,
                                int32_t(SoundWave::GetStaticType())));
    }
}
