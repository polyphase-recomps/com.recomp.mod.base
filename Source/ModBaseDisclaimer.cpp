/**
 * @file ModBaseDisclaimer.cpp
 * @brief The disclaimer scene's node and look (see ModBaseDisclaimer.h).
 */

#include "ModBaseDisclaimer.h"

#include "ModBaseModMap.h"
#include "ModBaseWidgets.h"

#include "AssetManager.h"
#include "Assets/Font.h"
#include "Assets/Scene.h"
#include "Assets/Texture.h"
#include "Engine.h"
#include "EngineTypes.h"
#include "Input/Input.h"
#include "Log.h"
#include "Nodes/Widgets/Quad.h"
#include "Nodes/Widgets/ScrollContainer.h"
#include "Nodes/Widgets/Text.h"
#include "Stream.h"
#include "System/System.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

FORCE_LINK_DEF(RecompDisclaimer);
DEFINE_NODE(RecompDisclaimer, Widget);

namespace
{
constexpr float kLayoutPadding = 16.0f; // the content column's padding (ModDisclaimer_Generate)
constexpr float kButtonH = 34.0f;

std::vector<RecompDisclaimer*>& Nodes()
{
    static std::vector<RecompDisclaimer*> sNodes;
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

void SetFloatProperty(Node* node, const char* name, float value)
{
    std::vector<Property> props;
    node->GatherProperties(props);
    for (Property& p : props)
    {
        if (p.mName == name && p.GetType() == DatumType::Float) p.SetFloat(value);
    }
}

// An ArrayWidget column as tall as its children (hidden ones keep their place too).
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

Text* FindText(Node* root, const char* name)
{
    Node* node = root != nullptr ? root->FindChild(name, true) : nullptr;
    return node != nullptr ? node->As<Text>() : nullptr;
}

RecompButton* FindButton(Node* root, const char* name)
{
    Node* node = root != nullptr ? root->FindChild(name, true) : nullptr;
    return node != nullptr ? node->As<RecompButton>() : nullptr;
}

std::string AgreedSaveName(const std::string& saveName)
{
    return (saveName.empty() ? std::string("recomp") : saveName) + ".agreed";
}
}

// ---- the node ------------------------------------------------------------------------------
RecompDisclaimer* RecompDisclaimer::Find(Node* from)
{
    const std::vector<RecompDisclaimer*>& all = Nodes();
    if (from != nullptr)
    {
        for (RecompDisclaimer* d : all)
        {
            Node* root = d->GetParent();
            if (root != nullptr && IsInside(from, root)) return d;
        }
    }
    return all.empty() ? nullptr : all.front();
}

void RecompDisclaimer::Start()
{
    Widget::Start();
    std::vector<RecompDisclaimer*>& all = Nodes();
    if (std::find(all.begin(), all.end(), this) == all.end()) all.push_back(this);
    mAccepted = WasAccepted();
    mDone = false;
    ShowPage(0);
}

void RecompDisclaimer::Stop()
{
    std::vector<RecompDisclaimer*>& all = Nodes();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
    Widget::Stop();
}

void RecompDisclaimer::Destroy()
{
    std::vector<RecompDisclaimer*>& all = Nodes();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
    Widget::Destroy();
}

void RecompDisclaimer::Tick(float deltaTime)
{
    Widget::Tick(deltaTime);
    FitText();
    if (mDone || !mAccepted)
    {
        return;
    }
    // accepted before: each page for a moment (A / Start skips it)
    mTimer -= deltaTime;
    if (mTimer <= 0.0f || INP_IsGamepadButtonJustDown(GAMEPAD_A, 0) || INP_IsGamepadButtonJustDown(GAMEPAD_START, 0))
    {
        if (mPage + 1 < mPageCount) ShowPage(mPage + 1);
        else Finish();
    }
}

void RecompDisclaimer::EditorTick(float deltaTime)
{
    Widget::EditorTick(deltaTime);
    FitText(); // the editor's preview as the game shows it
}

void RecompDisclaimer::ShowPage(int index)
{
    const int count = std::max(1, std::min((int)mPageCount, kMaxPages));
    mPage = std::max(0, std::min(index, count - 1));
    mTimer = mHoldSeconds;
    Node* root = GetParent();
    if (Text* t = FindText(root, "Title")) t->SetText(mTitles[mPage]);
    if (Text* t = FindText(root, "Text")) t->SetText(mTexts[mPage]);
    if (Text* t = FindText(root, "Progress"))
    {
        char progress[32];
        snprintf(progress, sizeof(progress), "%d / %d", mPage + 1, count);
        t->SetText(count > 1 ? progress : "");
    }
    // accepted before: no buttons, the pages go on by themselves
    for (const char* name : {"Accept", "Decline"})
    {
        if (RecompButton* b = FindButton(root, name))
        {
            b->SetVisible(!mAccepted);
            b->SetHeight(mAccepted ? 0.0f : kButtonH);
        }
    }
    FitText();
}

// The page's text as tall as it wraps to, the columns around it, and the content in the middle
// of a taller view (as the launcher's).
void RecompDisclaimer::FitText()
{
    Node* root = GetParent();
    Text* text = FindText(root, "Text");
    Node* layoutNode = root != nullptr ? root->FindChild("Layout", true) : nullptr;
    Widget* layout = layoutNode != nullptr ? layoutNode->As<Widget>() : nullptr;
    if (text == nullptr || layout == nullptr)
    {
        return;
    }
    const float scale = text->GetAbsoluteScale().y > 0.0f ? text->GetAbsoluteScale().y : 1.0f;
    const float height = std::ceil(text->GetTextHeight() / scale) + 4.0f;
    bool changed = std::fabs(text->GetHeight() - height) > 1.0f;
    if (changed) text->SetHeight(height);
    if (Node* node = root->FindChild("Buttons", true))
    {
        if (Widget* w = node->As<Widget>())
        {
            const float before = w->GetHeight();
            changed |= std::fabs(FitColumn(w) - before) > 0.5f;
        }
    }
    ScrollContainer* view = layout->GetParent() != nullptr ? layout->GetParent()->As<ScrollContainer>() : nullptr;
    const float padTop = FloatProperty(layout, "Padding Top", kLayoutPadding);
    if (changed)
    {
        // the column's height without the centring, then the centring again below
        SetFloatProperty(layout, "Padding Top", kLayoutPadding);
        FitColumn(layout);
    }
    if (view == nullptr)
    {
        return;
    }
    const glm::vec2 viewScale = view->GetAbsoluteScale();
    const float viewH = viewScale.y > 0.0f ? view->GetRect().mHeight / viewScale.y : 0.0f;
    if (viewH <= 0.0f)
    {
        return;
    }
    const float nowPad = changed ? kLayoutPadding : padTop;
    const float extraNow = std::max(0.0f, nowPad - kLayoutPadding);
    const float contentH = layout->GetHeight() - extraNow;
    const float extra = std::floor(std::max(0.0f, (viewH - contentH) * 0.5f));
    if (std::fabs(extra - extraNow) < 0.5f)
    {
        return;
    }
    SetFloatProperty(layout, "Padding Top", kLayoutPadding + extra);
    layout->SetHeight(contentH + extra);
}

std::string RecompDisclaimer::Fingerprint() const
{
    // FNV-1a of the pages: a change to their text asks the player again
    uint64_t hash = 1469598103934665603ull;
    auto add = [&hash](const std::string& s) {
        for (unsigned char c : s) hash = (hash ^ c) * 1099511628211ull;
        hash = (hash ^ 0xFF) * 1099511628211ull;
    };
    const int count = std::max(1, std::min((int)mPageCount, kMaxPages));
    for (int i = 0; i < count; ++i)
    {
        add(mTitles[i]);
        add(mTexts[i]);
    }
    char text[24];
    snprintf(text, sizeof(text), "%016llx", (unsigned long long)hash);
    return text;
}

bool RecompDisclaimer::WasAccepted() const
{
    const std::string name = AgreedSaveName(mSaveName);
    std::string saved;
#if EDITOR
    std::ifstream file(GetEngineState()->mProjectDirectory + "Saves/" + name, std::ios::binary);
    if (!file) return false;
    saved.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
#else
    Stream stream;
    if (!SYS_DoesSaveExist(name.c_str()) || !SYS_ReadSave(name.c_str(), stream)) return false;
    saved.assign(stream.GetData(), stream.GetSize());
#endif
    while (!saved.empty() && (saved.back() == '\n' || saved.back() == '\r' || saved.back() == ' ')) saved.pop_back();
    return saved == Fingerprint();
}

void RecompDisclaimer::SaveAccepted() const
{
    const std::string name = AgreedSaveName(mSaveName);
    const std::string fingerprint = Fingerprint();
#if EDITOR
    // SYS_WriteSave isn't exported to editor addons: the project's Saves folder, directly
    std::error_code ec;
    std::filesystem::create_directories(GetEngineState()->mProjectDirectory + "Saves", ec);
    std::ofstream file(GetEngineState()->mProjectDirectory + "Saves/" + name, std::ios::binary | std::ios::trunc);
    file << fingerprint;
    const bool ok = file.good();
#else
    Stream stream;
    stream.WriteBytes((const uint8_t*)fingerprint.data(), (uint32_t)fingerprint.size());
    const bool ok = SYS_WriteSave(name.c_str(), stream);
#endif
    if (!ok)
    {
        LogWarning("Disclaimer: could not save that it was accepted (%s)", name.c_str());
    }
}

void RecompDisclaimer::Accept()
{
    if (mDone)
    {
        return;
    }
    if (mPage + 1 < mPageCount)
    {
        ShowPage(mPage + 1);
        return;
    }
    if (!mAccepted)
    {
        SaveAccepted();
        mAccepted = true;
    }
    Finish();
}

void RecompDisclaimer::Decline()
{
#if EDITOR
    if (Text* t = FindText(GetParent(), "Progress")) t->SetText("Declined: a packaged game quits here");
    LogDebug("Disclaimer: declined (packaged games quit)");
#else
    GetEngineState()->mQuit = true;
#endif
}

void RecompDisclaimer::Finish()
{
    mDone = true;
    Scene* next = mNextScene.Get<Scene>();
    World* world = GetWorld();
    if (next != nullptr && world != nullptr)
    {
        world->LoadScene(next->GetName().c_str(), false);
        return;
    }
    // no scene to go to: the disclaimer steps aside
    if (RecompMenuController* controller = RecompMenuController::FindFor(this)) controller->Close();
    if (Node* root = GetParent())
    {
        if (Node* panel = root->FindChild("Panel", false)) panel->SetVisible(false);
        if (Node* background = root->FindChild("Background", false)) background->SetVisible(false);
    }
}

void RecompDisclaimer::SetPage(int index, const std::string& title, const std::string& text)
{
    if (index < 0 || index >= kMaxPages) return;
    mTitles[index] = title;
    mTexts[index] = text;
}

void RecompDisclaimer::SetPageCount(int count)
{
    mPageCount = std::max(1, std::min(count, (int)kMaxPages));
}

void RecompDisclaimer::SetNextScene(const AssetRef& scene)
{
    mNextScene = scene;
}

void RecompDisclaimer::SetHoldSeconds(float seconds)
{
    mHoldSeconds = seconds;
}

void RecompDisclaimer::SetSaveName(const std::string& name)
{
    mSaveName = name;
}

void RecompDisclaimer::GatherProperties(std::vector<Property>& outProps)
{
    Widget::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp Disclaimer");
    static const char* const kTitleNames[kMaxPages] = {"Page 1 Title", "Page 2 Title", "Page 3 Title", "Page 4 Title"};
    static const char* const kTextNames[kMaxPages] = {"Page 1 Text", "Page 2 Text", "Page 3 Text", "Page 4 Text"};
    outProps.push_back(Property(DatumType::Integer, "Page Count", this, &mPageCount));
    for (int i = 0; i < kMaxPages; ++i)
    {
        outProps.push_back(Property(DatumType::String, kTitleNames[i], this, &mTitles[i]));
        outProps.push_back(Property(DatumType::String, kTextNames[i], this, &mTexts[i]));
    }
    outProps.push_back(Property(DatumType::Asset, "Next Scene", this, &mNextScene, 1, nullptr,
                                int32_t(Scene::GetStaticType())));
    outProps.push_back(Property(DatumType::Float, "Hold Seconds", this, &mHoldSeconds));
    outProps.push_back(Property(DatumType::String, "Save Name", this, &mSaveName));
}

bool RecompDisclaimer_Command(const std::string& command, Node* from)
{
    RecompDisclaimer* disclaimer = RecompDisclaimer::Find(from);
    if (disclaimer == nullptr)
    {
        LogWarning("@disclaimer:%s: no RecompDisclaimer node in the running scene", command.c_str());
        return false;
    }
    if (command == "accept") disclaimer->Accept();
    else if (command == "decline") disclaimer->Decline();
    else
    {
        LogWarning("@disclaimer:%s: unknown command (accept, decline)", command.c_str());
        return false;
    }
    return true;
}

// ---- the look ------------------------------------------------------------------------------
void ModDisclaimer_ApplyLook(Node* root, const ModMap& map)
{
    if (root == nullptr)
    {
        return;
    }
    const ModStyle& s = map.mStyle;
    const ModDisclaimerSettings& d = map.mDisclaimer;
    ModStyle_Apply(root, s); // panel, buttons, fonts of the buttons

    Font* header = ModStyle_Font(s, s.mHeaderFont);
    Font* body = ModStyle_Font(s, s.mBodyFont);
    auto style = [&](const char* name, Font* font, float size, glm::vec4 color) {
        if (Text* t = FindText(root, name))
        {
            t->SetFont(font);
            t->SetTextSize(size);
            t->SetColor(color);
        }
    };
    style("Title", header, s.mTitleSize * 1.4f, s.mTitleColor);
    style("Progress", body, s.mNoteSize, s.mInfoColor);
    style("Text", body, s.mLabelSize, s.mLabelColor);
    if (Text* t = FindText(root, "Text")) t->EnableWordWrap(true);

    if (Node* node = root->FindChild("Background", false))
    {
        if (Quad* q = node->As<Quad>())
        {
            Texture* picture = d.mBackground.Get<Texture>();
            q->SetColor(picture != nullptr && !d.mTintBackground ? glm::vec4(1.0f) : d.mBackgroundColor);
            q->SetTexture(picture);
            q->SetVisible(d.mShowBackground);
        }
    }
    if (Node* node = root->FindChild("Panel", false))
    {
        Widget* panel = node->As<Widget>();
        if (panel != nullptr && d.mPanelFullScreen)
        {
            panel->SetAnchorMode(AnchorMode::FullStretch);
            panel->SetRatios(0.0f, 0.0f, 1.0f, 1.0f);
        }
    }

    RecompButton* accept = FindButton(root, "Accept");
    RecompButton* decline = FindButton(root, "Decline");
    if (accept != nullptr) accept->SetTextString(d.mAcceptLabel);
    if (decline != nullptr) decline->SetTextString(d.mDeclineLabel);
    if (accept != nullptr && decline != nullptr)
    {
        accept->SetNavDown(decline);
        decline->SetNavUp(accept);
    }

    for (uint32_t i = 0; i < root->GetNumChildren(); ++i)
    {
        Node* child = root->GetChild((int32_t)i);
        if (RecompDisclaimer* disclaimer = child->As<RecompDisclaimer>())
        {
            const int count = std::max(1, std::min((int)d.mPages.size(), (int)RecompDisclaimer::kMaxPages));
            disclaimer->SetPageCount(count);
            for (int p = 0; p < RecompDisclaimer::kMaxPages; ++p)
            {
                if (p < (int)d.mPages.size()) disclaimer->SetPage(p, d.mPages[p].mTitle, d.mPages[p].mText);
                else disclaimer->SetPage(p, "", "");
            }
            disclaimer->SetNextScene(d.mNextScene);
            disclaimer->SetHoldSeconds(d.mHoldSeconds);
            disclaimer->SetSaveName(map.SaveName());
            disclaimer->ShowPage(0); // the editor shows the first page
        }
        else if (RecompMenuController* controller = child->As<RecompMenuController>())
        {
            controller->SetPanelFit(d.mPanelFullScreen ? glm::vec2(0.0f) : d.mPanelSize, 16.0f, 0);
        }
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
            layout->SetAnchorMode(AnchorMode::TopLeft);
            layout->SetPosition(0.0f, 0.0f);
            SetFloatProperty(layout, "Padding Top", kLayoutPadding);
            FitColumn(layout);
        }
    }
}
