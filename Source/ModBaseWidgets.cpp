/**
 * @file ModBaseWidgets.cpp
 * @brief Widgets bound to the running game and to mod settings (see ModBaseWidgets.h).
 */

#include "ModBaseWidgets.h"

#include "ModBaseLauncher.h"
#include "ModBaseModMap.h"
#include "ModBaseProvider.h"
#include "ModBaseSettings.h"

#include "AssetManager.h"
#include "Assets/Font.h"
#include "Assets/Texture.h"
#include "Log.h"
#include "Input/Input.h"
#include "Nodes/Widgets/Quad.h"
#include "Nodes/Widgets/ScrollContainer.h"
#if __has_include("Input/PlayerInputSystem.h")
#include "Input/PlayerInputSystem.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>

FORCE_LINK_DEF(RecompText);
DEFINE_NODE(RecompText, Text);
FORCE_LINK_DEF(RecompButton);
DEFINE_NODE(RecompButton, Button);
FORCE_LINK_DEF(RecompBar);
DEFINE_NODE(RecompBar, ProgressBar);
FORCE_LINK_DEF(RecompMenuController);
DEFINE_NODE(RecompMenuController, Widget);

// ---- format ------------------------------------------------------------------------------
namespace
{
struct Token
{
    std::string name;
    int index = 0;
    std::string table;
    int width = 0;
    bool zeroPad = false;
    bool conditional = false;
    std::string yes, no;
};

Token ParseToken(const std::string& text)
{
    Token t;
    size_t end = text.find_first_of("[>:?");
    t.name = text.substr(0, end);
    while (end != std::string::npos && end < text.size())
    {
        const char c = text[end];
        if (c == '?')
        {
            const std::string rest = text.substr(end + 1);
            const size_t bar = rest.find('|');
            t.conditional = true;
            t.yes = rest.substr(0, bar);
            t.no = bar == std::string::npos ? std::string() : rest.substr(bar + 1);
            break;
        }
        const size_t next = text.find_first_of("[>:?", end + 1);
        const std::string part = text.substr(end + 1, next == std::string::npos ? std::string::npos : next - end - 1);
        if (c == '[') t.index = atoi(part.c_str());
        else if (c == '>') t.table = part;
        else if (c == ':')
        {
            t.zeroPad = !part.empty() && part[0] == '0';
            t.width = atoi(part.c_str());
        }
        end = next;
    }
    return t;
}

bool ValueText(const std::string& name, int index, const Token& t, std::string& out)
{
    RecompProvider* provider = Recomp_FindProvider();
    RecompValue v;
    if (provider == nullptr || !provider->IsLive() || !provider->Get(name, index, v))
    {
        return false;
    }
    if (v.isText)
    {
        out = v.text;
        return true;
    }
    char buf[48];
    const double n = v.number;
    if (n == (double)(long long)n)
    {
        if (t.width > 0)
            snprintf(buf, sizeof(buf), t.zeroPad ? "%0*lld" : "%*lld", t.width, (long long)n);
        else
            snprintf(buf, sizeof(buf), "%lld", (long long)n);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.2f", n);
    }
    out = buf;
    return true;
}

bool Resolve(const Token& t, std::string& out)
{
    if (!t.name.empty() && t.name[0] == '@')
    {
        // a mod setting: {@id} its value text, {@id.label} its label
        std::string id = t.name.substr(1);
        if (id.compare(0, 9, "launcher.") == 0)
        {
            return RecompLauncher_Token(id.substr(9), out);
        }
        bool label = false;
        if (id.size() > 6 && id.compare(id.size() - 6, 6, ".label") == 0)
        {
            id.resize(id.size() - 6);
            label = true;
        }
        const ModEntry* e = ModSettings::Get().FindEntry(id);
        if (e == nullptr)
        {
            return false;
        }
        out = label ? e->mLabel : ModSettings::Get().ValueText(id);
        return out != "--";
    }
    if (t.conditional)
    {
        double value = 0;
        if (!Recomp_GetNumber(t.name, t.index, value))
        {
            return false;
        }
        out = value != 0 ? t.yes : t.no;
        return true;
    }
    if (t.table.empty())
    {
        return ValueText(t.name, t.index, t, out);
    }
    double lookup = 0;
    if (!Recomp_GetNumber(t.name, t.index, lookup) || lookup < 0)
    {
        return false;
    }
    return ValueText(t.table, (int)lookup, t, out);
}

std::vector<int> ParseArgs(const std::string& text)
{
    std::vector<int> args;
    const char* p = text.c_str();
    while (*p)
    {
        char* end = nullptr;
        const long v = strtol(p, &end, 0);
        if (end == p)
        {
            ++p;
            continue;
        }
        args.push_back((int)v);
        p = end;
    }
    return args;
}

bool sInputBlocked = false;

// The RecompMenuController of the UI a node is in (a child of one of its ancestors).
RecompMenuController* FindController(Node* node)
{
    for (Node* n = node; n != nullptr; n = n->GetParent())
    {
        for (uint32_t i = 0; i < n->GetNumChildren(); ++i)
        {
            if (RecompMenuController* c = n->GetChild((int32_t)i)->As<RecompMenuController>())
            {
                return c;
            }
        }
    }
    return nullptr;
}

void CloseMenu(Node* from)
{
    if (RecompMenuController* c = FindController(from))
    {
        c->Close();
    }
}

// The engine's Button selects whatever button is under the mouse pointer, every frame: a pointer
// left resting on a button (a wide one in the middle of the window) took the selection back
// from the gamepad each frame, and moving down got stuck there. Buttons follow the pointer
// only while it is in use: it moved, clicked or scrolled since the last gamepad / arrow key.
bool PointerInUse()
{
    static uint32_t sFrame = UINT32_MAX;
    static bool sInUse = true;
    static bool sHavePos = false;
    static int32_t sX = 0;
    static int32_t sY = 0;
    EngineState* engine = GetEngineState();
    const uint32_t frame = engine != nullptr ? engine->mFrameNumber : 0;
    if (frame == sFrame)
    {
        return sInUse;
    }
    sFrame = frame;
    int32_t x = 0;
    int32_t y = 0;
    INP_GetMousePosition(x, y);
    const bool moved = sHavePos && (x != sX || y != sY);
    sHavePos = true;
    sX = x;
    sY = y;
    if (moved || INP_IsPointerJustDown(0) || INP_IsMouseButtonJustDown(MOUSE_RIGHT) || INP_GetScrollWheelDelta() != 0)
    {
        sInUse = true;
        return sInUse;
    }
    for (int32_t b = 0; b < GAMEPAD_BUTTON_COUNT && sInUse; ++b)
    {
        if (INP_IsGamepadButtonJustDown(b, 0))
        {
            sInUse = false;
        }
    }
    if (INP_IsKeyJustDown(POLYPHASE_KEY_UP) || INP_IsKeyJustDown(POLYPHASE_KEY_DOWN) ||
        INP_IsKeyJustDown(POLYPHASE_KEY_LEFT) || INP_IsKeyJustDown(POLYPHASE_KEY_RIGHT))
    {
        sInUse = false;
    }
    return sInUse;
}

// The first button in a subtree, skipping hidden widgets (they don't tick, so a hidden
// selected button would leave the gamepad stuck).
Button* FirstVisibleButton(Node* node)
{
    if (node == nullptr)
    {
        return nullptr;
    }
    if (Widget* w = node->As<Widget>())
    {
        if (!w->IsVisible()) return nullptr;
    }
    if (Button* b = node->As<Button>())
    {
        return b;
    }
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        if (Button* b = FirstVisibleButton(node->GetChild((int32_t)i))) return b;
    }
    return nullptr;
}

// Page tabs: the nearest ancestor with a "Pages" child shows only the page named
// "Page_<name>". Its sibling "Footer" buttons then navigate up to that page's last row.
void ShowPage(Node* from, const std::string& page)
{
    for (Node* n = from->GetParent(); n != nullptr; n = n->GetParent())
    {
        Node* pages = n->FindChild("Pages", false);
        if (pages == nullptr)
        {
            continue;
        }
        const std::string wanted = "Page_" + page;
        Node* shown = nullptr;
        for (uint32_t i = 0; i < pages->GetNumChildren(); ++i)
        {
            if (Widget* w = pages->GetChild((int32_t)i)->As<Widget>())
            {
                w->SetVisible(w->GetName() == wanted);
                if (w->GetName() == wanted) shown = w;
            }
        }
        Node* footer = n->FindChild("Footer", false);
        if (shown != nullptr && footer != nullptr)
        {
            Node* list = shown->FindChild("List", false);
            if (list == nullptr) list = shown;
            Button* last = nullptr;
            for (int32_t r = (int32_t)list->GetNumChildren() - 1; r >= 0 && last == nullptr; --r)
            {
                last = FirstVisibleButton(list->GetChild(r));
            }
            Button* tab = from->As<Button>();
            for (uint32_t i = 0; i < footer->GetNumChildren(); ++i)
            {
                if (Button* btn = footer->GetChild((int32_t)i)->As<Button>())
                {
                    btn->SetNavUp(last != nullptr ? last : tab);
                }
            }
        }
        return;
    }
}

// Scrolls every ScrollContainer between `button` and `stop` so the button is in view.
void KeepInView(Button* button, Node* stop)
{
    const float kEdge = 6.0f;
    for (Node* n = button->GetParent(); n != nullptr && n != stop; n = n->GetParent())
    {
        ScrollContainer* scroll = n->As<ScrollContainer>();
        if (scroll == nullptr)
        {
            continue;
        }
        const Rect view = scroll->GetRect();
        const Rect r = button->GetRect();
        const glm::vec2 s = scroll->GetAbsoluteScale();
        // a page shown this frame may not have its rects yet: wait a frame
        if (s.x <= 0.0f || s.y <= 0.0f || view.mHeight <= 0.0f || r.mHeight <= 0.0f)
        {
            continue;
        }
        glm::vec2 offset = scroll->GetScrollOffset();
        if (scroll->CanScrollVertically())
        {
            if (r.mY < view.mY) offset.y -= (view.mY - r.mY) / s.y + kEdge;
            else if (r.mY + r.mHeight > view.mY + view.mHeight) offset.y += (r.mY + r.mHeight - view.mY - view.mHeight) / s.y + kEdge;
        }
        if (scroll->CanScrollHorizontally())
        {
            if (r.mX < view.mX) offset.x -= (view.mX - r.mX) / s.x + kEdge;
            else if (r.mX + r.mWidth > view.mX + view.mWidth) offset.x += (r.mX + r.mWidth - view.mX - view.mWidth) / s.x + kEdge;
        }
        if (offset != scroll->GetScrollOffset())
        {
            scroll->SetScrollOffset(offset);
        }
    }
}

// The visible ScrollContainer under `node` that scrolls up/down (the shown page).
ScrollContainer* VisibleVerticalScroll(Node* node)
{
    if (Widget* w = node->As<Widget>())
    {
        if (!w->IsVisible()) return nullptr;
    }
    if (ScrollContainer* s = node->As<ScrollContainer>())
    {
        if (s->CanScrollVertically()) return s;
    }
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        if (ScrollContainer* s = VisibleVerticalScroll(node->GetChild((int32_t)i))) return s;
    }
    return nullptr;
}
}

std::string RecompFormat(const std::string& format, bool* missing)
{
    std::string out;
    bool anyMissing = false;
    for (size_t i = 0; i < format.size(); ++i)
    {
        const char c = format[i];
        if ((c == '{' || c == '}') && i + 1 < format.size() && format[i + 1] == c)
        {
            out.push_back(c);
            ++i;
            continue;
        }
        if (c != '{')
        {
            out.push_back(c);
            continue;
        }
        const size_t close = format.find('}', i + 1);
        if (close == std::string::npos)
        {
            out.append(format, i, std::string::npos);
            break;
        }
        std::string value;
        if (Resolve(ParseToken(format.substr(i + 1, close - i - 1)), value))
        {
            out += value;
        }
        else
        {
            out += "--";
            anyMissing = true;
        }
        i = close;
    }
    if (missing)
    {
        *missing = anyMissing;
    }
    return out;
}

bool RecompIsLive()
{
    RecompProvider* provider = Recomp_FindProvider();
    return provider != nullptr && provider->IsLive();
}

bool Recomp_IsInputCaptured()
{
    return sInputBlocked || RecompMenuController::IsCapturingInput();
}

void Recomp_SetInputBlocked(bool blocked)
{
    sInputBlocked = blocked;
}

bool Recomp_PointerInUse()
{
    return PointerInUse();
}

// ---- RecompText --------------------------------------------------------------------------
void RecompText::Tick(float deltaTime)
{
    Text::Tick(deltaTime);
    // settings ({@...}) resolve without a game; game tokens need one
    const bool settingsOnly = mFormat.find('{') == std::string::npos || mFormat.find("{@") != std::string::npos;
    if (mFormat.empty() || (!RecompIsLive() && !settingsOnly))
    {
        return;
    }
    bool missing = false;
    const std::string text = RecompFormat(mFormat, &missing);
    if (!RecompIsLive() && missing)
    {
        return;
    }
    const std::string shown = (missing && mHideIfMissing) ? std::string() : text;
    if (shown != GetText())
    {
        SetText(shown);
    }
}

void RecompText::GatherProperties(std::vector<Property>& outProps)
{
    Text::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp");
    outProps.push_back(Property(DatumType::String, "Format", this, &mFormat));
    outProps.push_back(Property(DatumType::Bool, "Hide If Missing", this, &mHideIfMissing));
}

void RecompText::SetFormat(const std::string& format)
{
    mFormat = format;
    SetText(format);
}

void RecompText::SetHideIfMissing(bool hide)
{
    mHideIfMissing = hide;
}

// ---- RecompButton ------------------------------------------------------------------------
void RecompButton::Activate()
{
    Button::Activate();
    if (!mSetting.empty())
    {
        ModSettings& settings = ModSettings::Get();
        if (mSetting == "@save") settings.Save();
        else if (mSetting == "@reset") settings.ResetAll();
        else if (mSetting == "@close") CloseMenu(this);
        else if (mSetting.compare(0, 6, "@page:") == 0) ShowPage(this, mSetting.substr(6));
        else if (mSetting.compare(0, 10, "@launcher:") == 0) RecompLauncher_Command(mSetting.substr(10), this);
        else settings.Step(mSetting, mDirection);
        return;
    }
    RecompProvider* provider = Recomp_FindProvider();
    if (provider == nullptr || !provider->IsLive())
    {
        return;
    }
    if (!mRequest.empty())
    {
        mPending = provider->Request(mRequest, ParseArgs(mArguments));
    }
    else if (!mToggleVariable.empty())
    {
        RecompValue v;
        if (provider->Get(mToggleVariable, 0, v))
        {
            provider->Set(mToggleVariable, 0, RecompValue::Number(v.number != 0 ? 0 : mToggleOnValue));
        }
    }
    else if (!mVariable.empty())
    {
        RecompValue v;
        if (provider->Get(mVariable, 0, v))
        {
            double next = v.number + mStep;
            next = next < mMin ? mMin : (next > mMax ? mMax : next);
            provider->Set(mVariable, 0, RecompValue::Number(next));
        }
    }
}

void RecompButton::Tick(float deltaTime)
{
    // the pointer only hovers / clicks while it is in use (see PointerInUse)
    const bool handleMouse = sHandleMouseInput;
    if (!PointerInUse())
    {
        sHandleMouseInput = false;
    }
    Button::Tick(deltaTime);
    sHandleMouseInput = handleMouse;
    const bool selected = Button::GetSelectedButton() == this;
    if (selected != mHighlighted)
    {
        mHighlighted = selected;
        // a page tab shows its page as soon as it's selected, so moving down from it
        // lands on that (visible) page
        if (selected && mSetting.compare(0, 6, "@page:") == 0)
        {
            ShowPage(this, mSetting.substr(6));
        }
        if (Quad* quad = GetQuad())
        {
            quad->SetBorderColor(mHighlightColor);
            quad->SetBorderWidth(selected ? mHighlightWidth : 0.0f);
        }
    }
    if (!mLabelFormat.empty())
    {
        bool missing = false;
        const std::string label = RecompFormat(mLabelFormat, &missing);
        if ((RecompIsLive() || !missing) && label != GetTextString())
        {
            SetTextString(label);
        }
    }
    int result = 0;
    RecompProvider* provider = Recomp_FindProvider();
    if (mPending != 0 && provider != nullptr && provider->Result(mPending, result))
    {
        if (result < 0)
        {
            LogWarning("%s: %s answered %d (busy or not possible right now)", GetName().c_str(), mRequest.c_str(),
                       result);
        }
        mPending = 0;
    }
}

void RecompButton::PreRender()
{
    Button::PreRender();
    Quad* quad = GetQuad();
    const ObjectFit fit = mTextureFit < uint8_t(ObjectFit::Count) ? ObjectFit(mTextureFit) : ObjectFit::Contain;
    if (quad != nullptr && quad->GetObjectFit() != fit)
    {
        quad->SetObjectFit(fit);
    }
}

void RecompButton::SetTextureFit(uint8_t fit)
{
    mTextureFit = fit;
    MarkDirty();
}

void RecompButton::SetHighlight(glm::vec4 color, float width)
{
    mHighlightColor = color;
    mHighlightWidth = width;
    if (Quad* quad = GetQuad())
    {
        quad->SetBorderColor(mHighlightColor);
        quad->SetBorderWidth(mHighlighted ? mHighlightWidth : 0.0f);
    }
}

void RecompButton::GatherProperties(std::vector<Property>& outProps)
{
    Button::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp");
    outProps.push_back(Property(DatumType::String, "Label Format", this, &mLabelFormat));
    outProps.push_back(Property(DatumType::Color, "Highlight Color", this, &mHighlightColor));
    outProps.push_back(Property(DatumType::Float, "Highlight Width", this, &mHighlightWidth));
    static const char* kFits[] = {"Fill", "Contain", "Cover", "None"};
    outProps.push_back(Property(DatumType::Byte, "Texture Fit", this, &mTextureFit, 1, nullptr, NULL_DATUM,
                                int32_t(ObjectFit::Count), kFits));
    outProps.push_back(Property(DatumType::String, "Setting", this, &mSetting));
    outProps.push_back(Property(DatumType::Integer, "Direction", this, &mDirection));
    outProps.push_back(Property(DatumType::String, "Request", this, &mRequest));
    outProps.push_back(Property(DatumType::String, "Arguments", this, &mArguments));
    outProps.push_back(Property(DatumType::String, "Toggle Variable", this, &mToggleVariable));
    outProps.push_back(Property(DatumType::Integer, "Toggle On Value", this, &mToggleOnValue));
    outProps.push_back(Property(DatumType::String, "Step Variable", this, &mVariable));
    outProps.push_back(Property(DatumType::Integer, "Step", this, &mStep));
    outProps.push_back(Property(DatumType::Integer, "Step Min", this, &mMin));
    outProps.push_back(Property(DatumType::Integer, "Step Max", this, &mMax));
}

void RecompButton::SetSetting(const std::string& id, int32_t direction)
{
    mSetting = id;
    mDirection = direction;
}

void RecompButton::SetRequest(const std::string& request, const std::string& arguments)
{
    mRequest = request;
    mArguments = arguments;
}

void RecompButton::SetToggle(const std::string& variable, int32_t onValue)
{
    mToggleVariable = variable;
    mToggleOnValue = onValue;
}

void RecompButton::SetStep(const std::string& variable, int32_t step, int32_t minValue, int32_t maxValue)
{
    mVariable = variable;
    mStep = step;
    mMin = minValue;
    mMax = maxValue;
}

void RecompButton::SetLabelFormat(const std::string& format)
{
    mLabelFormat = format;
}

// ---- RecompBar ---------------------------------------------------------------------------
void RecompBar::Tick(float deltaTime)
{
    ProgressBar::Tick(deltaTime);
    double value = 0, maxValue = 0;
    if (!mMaxVariable.empty() && Recomp_GetNumber(mMaxVariable, 0, maxValue) && maxValue > 0)
    {
        SetMaxValue((float)maxValue);
    }
    if (!mVariable.empty() && Recomp_GetNumber(mVariable, 0, value))
    {
        SetValue((float)value);
    }
}

void RecompBar::GatherProperties(std::vector<Property>& outProps)
{
    ProgressBar::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp");
    outProps.push_back(Property(DatumType::String, "Variable", this, &mVariable));
    outProps.push_back(Property(DatumType::String, "Max Variable", this, &mMaxVariable));
}

void RecompBar::SetVariables(const std::string& variable, const std::string& maxVariable)
{
    mVariable = variable;
    mMaxVariable = maxVariable;
}

// ---- style -------------------------------------------------------------------------------
namespace
{
void StyleText(Text* t, Font* font, float size, glm::vec4 color)
{
    t->SetFont(font);
    t->SetTextSize(size);
    t->SetColor(color);
}

bool HasButton(Node* row)
{
    for (uint32_t i = 0; i < row->GetNumChildren(); ++i)
    {
        if (row->GetChild((int32_t)i)->As<Button>() != nullptr) return true;
    }
    return false;
}

struct StyleFonts
{
    Font* header;
    Font* body;
    Font* button;
};

void ApplyStyle(Node* node, const ModStyle& s, const StyleFonts& fonts)
{
    if (node->IsTransient())
    {
        return; // a widget's own internals (a button's quad and text)
    }
    const std::string& name = node->GetName();
    Node* parent = node->GetParent();
    if (RecompButton* b = node->As<RecompButton>())
    {
        b->SetNormalTexture(s.mButtonTextures[ModStyle::Normal].Get<Texture>());
        b->SetHoveredTexture(s.mButtonTextures[ModStyle::Hovered].Get<Texture>());
        b->SetPressedTexture(s.mButtonTextures[ModStyle::Pressed].Get<Texture>());
        b->SetLockedTexture(s.mButtonTextures[ModStyle::Locked].Get<Texture>());
        b->SetNormalColor(s.mButtonColors[ModStyle::Normal]);
        b->SetHoveredColor(s.mButtonColors[ModStyle::Hovered]);
        b->SetPressedColor(s.mButtonColors[ModStyle::Pressed]);
        b->SetLockedColor(s.mButtonColors[ModStyle::Locked]);
        b->SetHighlight(s.mHighlightColor, s.mShowHighlight ? s.mHighlightWidth : 0.0f);
        b->SetUvScale(s.mButtonUvScale);
        b->SetUvOffset(s.mButtonUvOffset);
        b->SetTextureFit(s.mButtonFit);
        // off: every state looks as Normal does (its color, or its texture as it is)
        b->SetUseQuadStateColor(s.mButtonStateTint);
        if (!s.mButtonStateTint)
        {
            if (Quad* q = b->GetQuad()) q->SetColor(s.mButtonColors[ModStyle::Normal]);
        }
        const bool tab = parent != nullptr && parent->GetName() == "Tabs";
        if (Text* t = b->GetText())
        {
            StyleText(t, fonts.button, tab ? s.mTabTextSize : s.mButtonTextSize, s.mButtonTextColor);
        }
        b->MarkDirty();
        return;
    }
    if (Text* t = node->As<Text>())
    {
        if (name == "Title") StyleText(t, fonts.header, s.mTitleSize, s.mTitleColor);
        else if (name == "Note") StyleText(t, fonts.body, s.mNoteSize, s.mInfoColor);
        else if (name == "Value") StyleText(t, fonts.body, s.mValueSize, s.mValueColor);
        else if (name == "Label")
            StyleText(t, fonts.body, s.mLabelSize, (parent != nullptr && HasButton(parent)) ? s.mLabelColor : s.mInfoColor);
        else t->SetFont(fonts.body);
    }
    else if (name == "Panel")
    {
        if (Quad* q = node->As<Quad>())
        {
            Texture* texture = s.mPanelTexture.Get<Texture>();
            q->SetColor(texture != nullptr && !s.mTintPanel ? glm::vec4(1.0f) : s.mPanelColor);
            q->SetTexture(texture);
        }
    }
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        ApplyStyle(node->GetChild((int32_t)i), s, fonts);
    }
}
}

Font* ModStyle_Font(const ModStyle& style, const AssetRef& role)
{
    Font* font = role.Get<Font>();
    if (font == nullptr) font = style.mFont.Get<Font>();
    if (font == nullptr) font = LoadAsset<Font>("F_Roboto32"); // the engine's default text font
    return font;
}

void ModStyle_Apply(Node* root, const ModStyle& style)
{
    if (root == nullptr)
    {
        return;
    }
    const StyleFonts fonts = {ModStyle_Font(style, style.mHeaderFont), ModStyle_Font(style, style.mBodyFont),
                              ModStyle_Font(style, style.mButtonFont)};
    ApplyStyle(root, style, fonts);
}

// ---- input actions -----------------------------------------------------------------------
std::vector<std::string> RecompInputActions()
{
    std::vector<std::string> out;
#if defined(POLYPHASE_PLAYER_INPUT_EXPORTED)
    if (PlayerInputSystem* input = PlayerInputSystem::Get())
    {
        for (const InputAction& a : input->GetActions())
        {
            out.push_back(a.category.empty() ? a.name : a.category + "/" + a.name);
        }
    }
#endif
    return out;
}

bool RecompActionJustPressed(const std::string& action)
{
#if defined(POLYPHASE_PLAYER_INPUT_EXPORTED)
    PlayerInputSystem* input = PlayerInputSystem::Get();
    if (input == nullptr || action.empty()) return false;
    const size_t slash = action.find('/');
    if (slash != std::string::npos)
    {
        return input->WasActionJustActivated(action.substr(0, slash), action.substr(slash + 1));
    }
    for (const InputAction& a : input->GetActions())
    {
        if (a.name == action && input->WasActionJustActivated(a.category, a.name)) return true;
    }
#else
    (void)action;
#endif
    return false;
}

const char* RecompGamepadButtonName(int32_t gamepadButton)
{
    static const struct { int32_t code; const char* name; } kNames[] = {
        {GAMEPAD_SELECT, "Select"}, {GAMEPAD_START, "Start"}, {GAMEPAD_HOME, "Home"},
        {GAMEPAD_THUMBR, "Right stick click"}, {GAMEPAD_THUMBL, "Left stick click"},
        {GAMEPAD_Z, "Z"}, {GAMEPAD_A, "A"}, {GAMEPAD_B, "B"}, {GAMEPAD_X, "X"}, {GAMEPAD_Y, "Y"},
        {GAMEPAD_L1, "L1"}, {GAMEPAD_R1, "R1"}, {GAMEPAD_L2, "L2"}, {GAMEPAD_R2, "R2"},
    };
    if (gamepadButton < 0) return "none";
    for (const auto& n : kNames)
    {
        if (n.code == gamepadButton) return n.name;
    }
    return "button";
}

// ---- RecompMenuController ----------------------------------------------------------------
namespace
{
std::vector<RecompMenuController*>& Controllers()
{
    static std::vector<RecompMenuController*> sControllers;
    return sControllers;
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

// The panel property, else the sibling named "Panel", else the parent (older UIs).
Widget* RecompMenuController::Target()
{
    if (Widget* panel = mPanel.Get()) return panel;
    Node* parent = GetParent();
    if (parent == nullptr) return nullptr;
    for (uint32_t i = 0; i < parent->GetNumChildren(); ++i)
    {
        Node* sibling = parent->GetChild((int32_t)i);
        if (sibling != this && sibling->GetName() == "Panel")
        {
            if (Widget* w = sibling->As<Widget>()) return w;
        }
    }
    return parent->As<Widget>();
}

bool RecompMenuController::TargetVisible()
{
    Widget* target = Target();
    return target != nullptr && target->IsVisible(true);
}

void RecompMenuController::Start()
{
    Widget::Start();
    std::vector<RecompMenuController*>& all = Controllers();
    if (std::find(all.begin(), all.end(), this) == all.end())
    {
        all.push_back(this);
    }
    mWasVisible = false;
    mLoggedSetup = false;
    if (Widget* target = Target())
    {
        target->SetVisible(mStartVisible);
    }
}

void RecompMenuController::Stop()
{
    std::vector<RecompMenuController*>& all = Controllers();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
    Widget::Stop();
}

void RecompMenuController::Destroy()
{
    std::vector<RecompMenuController*>& all = Controllers();
    all.erase(std::remove(all.begin(), all.end(), this), all.end());
    Widget::Destroy();
}

void RecompMenuController::Open()
{
    if (Widget* target = Target()) target->SetVisible(true);
}

void RecompMenuController::Close()
{
    if (Widget* target = Target()) target->SetVisible(false);
}

void RecompMenuController::Toggle()
{
    if (IsOpen()) Close();
    else Open();
}

bool RecompMenuController::IsOpen() const
{
    return const_cast<RecompMenuController*>(this)->TargetVisible();
}

bool RecompMenuController::IsInHomeMenu() const
{
    return mInHomeMenu;
}

const std::string& RecompMenuController::GetTitle() const
{
    return mTitle;
}

void RecompMenuController::Setup(const std::string& title, bool startVisible, bool captureInput, Node* firstButton)
{
    mTitle = title;
    mStartVisible = startVisible;
    mCaptureInput = captureInput;
    mFirstButton = ResolveWeakPtr<Button>(firstButton);
}

void RecompMenuController::SetBoundVariable(const std::string& name)
{
    mBoundVariable = name;
}

void RecompMenuController::SetInHomeMenu(bool inHomeMenu)
{
    mInHomeMenu = inHomeMenu;
}

void RecompMenuController::SetToggleButton(int32_t gamepadButton)
{
    mToggleButton = gamepadButton;
}

void RecompMenuController::SetToggleAction(const std::string& action)
{
    mToggleAction = action;
}

void RecompMenuController::SetCloseAction(const std::string& action)
{
    mCloseAction = action;
}

void RecompMenuController::SetCloseOnBack(bool closeOnBack)
{
    mCloseOnBack = closeOnBack;
}

void RecompMenuController::SetPanel(Node* panel)
{
    mPanel = ResolveWeakPtr<Widget>(panel);
}

void RecompMenuController::SetPanelFit(glm::vec2 maxSize, float margin, int32_t align)
{
    mMaxPanelSize = maxSize;
    mPanelMargin = margin;
    mPanelAlign = align;
    mFitMargins = {-1.0f, -1.0f, -1.0f, -1.0f};
}

const std::vector<RecompMenuController*>& RecompMenuController::GetAll()
{
    return Controllers();
}

RecompMenuController* RecompMenuController::FindFor(Node* node)
{
    return FindController(node);
}

RecompMenuController* RecompMenuController::FindSettingsMenu()
{
    RecompMenuController* first = nullptr;
    for (RecompMenuController* c : Controllers())
    {
        Node* root = c->GetParent();
        if (root != nullptr && root->FindChild("Pages", true) != nullptr) return c;
        // (a launcher's own menu is not a settings menu)
        if (first == nullptr && (root == nullptr || root->FindChild("Launcher", false) == nullptr)) first = c;
    }
    return first;
}

bool RecompMenuController::IsCapturingInput()
{
    for (RecompMenuController* c : Controllers())
    {
        if (c->mCaptureInput && c->TargetVisible()) return true;
    }
    return false;
}

void RecompMenuController::Tick(float deltaTime)
{
    Widget::Tick(deltaTime);
    if (!mLoggedSetup)
    {
        // says in the log that the UI is in the running scene and what opens it
        mLoggedSetup = true;
        std::string how;
        if (mToggleButton >= 0) how = std::string("gamepad ") + RecompGamepadButtonName(mToggleButton);
        if (!mToggleAction.empty())
        {
            how += how.empty() ? "" : ", ";
            how += "action '" + mToggleAction + "'";
#if !defined(POLYPHASE_PLAYER_INPUT_EXPORTED)
            how += " (needs an engine that exports PlayerInputSystem)";
#endif
        }
        if (!mBoundVariable.empty()) how += (how.empty() ? "" : ", ") + std::string("variable ") + mBoundVariable;
        if (mInHomeMenu) how += (how.empty() ? "" : ", ") + std::string("the HOME menu");
        Widget* shown = Target();
        LogDebug("Recomp menu '%s': opens with %s (shows '%s')", mTitle.c_str(), how.empty() ? "scripts only" : how.c_str(),
                 shown ? shown->GetName().c_str() : "nothing");
        if (shown != nullptr && shown == GetParent())
        {
            LogWarning("Recomp menu '%s': it hides its own parent, so it stops ticking and can't reopen it. "
                       "Set its Panel property to a sibling widget.", mTitle.c_str());
        }
    }
    const bool togglePressed = (mToggleButton >= 0 && INP_IsGamepadButtonJustDown(mToggleButton, 0)) ||
                               (!mToggleAction.empty() && RecompActionJustPressed(mToggleAction));
    if (togglePressed)
    {
        Toggle();
        LogDebug("Recomp menu '%s': %s", mTitle.c_str(), IsOpen() ? "opened" : "closed");
    }

    Widget* target = Target();
    FitPanel();
    if (!mBoundVariable.empty())
    {
        SyncBoundVariable();
    }
    const bool visible = TargetVisible();
    if (visible && !mWasVisible)
    {
        // select a button once A is up, so the press that opened the UI doesn't press it
        mSelectPending = mCaptureInput;
        static uint32_t sOpenOrder = 0;
        mOpenOrder = ++sOpenOrder;
    }
    else if (!visible && mWasVisible)
    {
        mSelectPending = false;
        Button* selected = Button::GetSelectedButton();
        if (selected != nullptr && target != nullptr && IsInside(selected, target))
        {
            Button::SetSelectedButton(nullptr);
        }
    }
    mWasVisible = visible;

    if (!visible || !mCaptureInput || target == nullptr)
    {
        return;
    }
    // another UI opened over this one (the mod settings over a launcher) has the gamepad
    for (RecompMenuController* other : Controllers())
    {
        if (other != this && other->mCaptureInput && other->mOpenOrder > mOpenOrder && other->TargetVisible())
        {
            return;
        }
    }
    if ((mCloseOnBack && INP_IsGamepadButtonJustDown(GAMEPAD_B, 0)) ||
        (!mBoundVariable.empty() && INP_IsGamepadButtonJustDown(GAMEPAD_START, 0)) ||
        (!mCloseAction.empty() && RecompActionJustPressed(mCloseAction)))
    {
        Close();
        LogDebug("Recomp menu '%s': closed", mTitle.c_str());
        return;
    }
    if (mSelectPending && INP_IsGamepadButtonDown(GAMEPAD_A, 0))
    {
        return;
    }
    mSelectPending = false;
    Button* selected = Button::GetSelectedButton();
    // nothing selected, outside this UI, or hidden (a hidden button doesn't tick, so the
    // gamepad would be stuck on it): select the first button, else the first visible one
    if (selected == nullptr || !IsInside(selected, target) || !selected->IsVisible(true))
    {
        Button* first = mFirstButton.Get();
        selected = (first != nullptr && first->IsVisible(true)) ? first : FirstVisibleButton(target);
        Button::SetSelectedButton(selected);
    }
    if (selected != nullptr && !PointerInUse())
    {
        KeepInView(selected, target);
    }
    GamepadScroll(deltaTime, target);
}

// Right stick up / down scrolls the shown page (rows without buttons, long text).
void RecompMenuController::GamepadScroll(float deltaTime, Widget* target)
{
    const float dir = (INP_IsGamepadButtonDown(GAMEPAD_R_DOWN, 0) ? 1.0f : 0.0f) -
                      (INP_IsGamepadButtonDown(GAMEPAD_R_UP, 0) ? 1.0f : 0.0f);
    if (dir == 0.0f)
    {
        return;
    }
    if (ScrollContainer* scroll = VisibleVerticalScroll(target))
    {
        glm::vec2 offset = scroll->GetScrollOffset();
        offset.y += dir * mScrollSpeed * deltaTime;
        scroll->SetScrollOffset(offset);
    }
}

// Fits a full-stretch panel to the screen: margins at least mPanelMargin, at most
// mMaxPanelSize, aligned by mPanelAlign. Margins only change when the screen does.
void RecompMenuController::FitPanel()
{
    Widget* panel = mPanel.Get();
    if (panel == nullptr || mMaxPanelSize.x <= 0.0f || mMaxPanelSize.y <= 0.0f ||
        panel->GetAnchorMode() != AnchorMode::FullStretch)
    {
        return;
    }
    Widget* parent = panel->GetParentWidget();
    if (parent == nullptr)
    {
        return;
    }
    const Rect area = parent->GetRect();
    const glm::vec2 scale = parent->GetAbsoluteScale();
    if (area.mWidth <= 0.0f || area.mHeight <= 0.0f || scale.x <= 0.0f || scale.y <= 0.0f)
    {
        return;
    }
    const float w = area.mWidth / scale.x;
    const float h = area.mHeight / scale.y;
    const float spareX = std::max(mPanelMargin * 2.0f, w - mMaxPanelSize.x);
    const float spareY = std::max(mPanelMargin * 2.0f, h - mMaxPanelSize.y);
    float left = spareX * 0.5f;
    if (mPanelAlign == 1) left = mPanelMargin;
    if (mPanelAlign == 2) left = spareX - mPanelMargin;
    const glm::vec4 margins = {std::floor(left), std::floor(spareY * 0.5f), std::floor(spareX - left),
                               std::floor(spareY * 0.5f)};
    if (margins != mFitMargins)
    {
        mFitMargins = margins;
        panel->SetMargins(margins.x, margins.y, margins.z, margins.w);
    }
}

// The UI follows a game variable (a pause flag): shown while it's non-zero; closing the UI
// sets it back to 0.
void RecompMenuController::SyncBoundVariable()
{
    RecompProvider* provider = Recomp_FindProvider();
    RecompValue v;
    if (provider == nullptr || !provider->IsLive() || !provider->Get(mBoundVariable, 0, v))
    {
        mBoundOn = false;
        return;
    }
    const bool on = v.number != 0;
    if (on != mBoundOn)
    {
        mBoundOn = on;
        if (on) Open();
        else Close();
    }
    else if (on && mWasVisible && !TargetVisible())
    {
        provider->Set(mBoundVariable, 0, RecompValue::Number(0));
    }
}

void RecompMenuController::GatherProperties(std::vector<Property>& outProps)
{
    Widget::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp Menu");
    outProps.push_back(Property(DatumType::String, "Title", this, &mTitle));
    outProps.push_back(Property(DatumType::Bool, "Start Visible", this, &mStartVisible));
    outProps.push_back(Property(DatumType::Bool, "Capture Input", this, &mCaptureInput));
    outProps.push_back(Property(DatumType::Node, "First Button", this, &mFirstButton));
    outProps.push_back(Property(DatumType::Node, "Panel", this, &mPanel));
    outProps.push_back(Property(DatumType::Vector2D, "Max Panel Size", this, &mMaxPanelSize));
    outProps.push_back(Property(DatumType::Float, "Panel Margin", this, &mPanelMargin));
    outProps.push_back(Property(DatumType::Integer, "Panel Align", this, &mPanelAlign));
    outProps.push_back(Property(DatumType::Float, "Scroll Speed", this, &mScrollSpeed));
    outProps.push_back(Property(DatumType::Integer, "Toggle Button", this, &mToggleButton));
    outProps.push_back(Property(DatumType::String, "Toggle Action", this, &mToggleAction));
    outProps.push_back(Property(DatumType::String, "Close Action", this, &mCloseAction));
    outProps.push_back(Property(DatumType::Bool, "Close On Back", this, &mCloseOnBack));
    outProps.push_back(Property(DatumType::Bool, "In HOME Menu", this, &mInHomeMenu));
    outProps.push_back(Property(DatumType::String, "Bound Variable", this, &mBoundVariable));
}
