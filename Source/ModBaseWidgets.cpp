/**
 * @file ModBaseWidgets.cpp
 * @brief Widgets bound to the running game and to mod settings (see ModBaseWidgets.h).
 */

#include "ModBaseWidgets.h"

#include "ModBaseProvider.h"
#include "ModBaseSettings.h"

#include "Log.h"
#include "Input/Input.h"
#include "Nodes/Widgets/Quad.h"

#include <algorithm>
#include <cstdio>
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

// Page tabs: the nearest ancestor with a "Pages" child shows only the page named
// "Page_<name>" and selects that page's first button.
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
        for (uint32_t i = 0; i < pages->GetNumChildren(); ++i)
        {
            if (Widget* w = pages->GetChild((int32_t)i)->As<Widget>())
            {
                w->SetVisible(w->GetName() == wanted);
            }
        }
        return;
    }
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
    Button::Tick(deltaTime);
    const bool selected = Button::GetSelectedButton() == this;
    if (selected != mHighlighted)
    {
        mHighlighted = selected;
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

void RecompButton::GatherProperties(std::vector<Property>& outProps)
{
    Button::GatherProperties(outProps);
    SCOPED_CATEGORY("Recomp");
    outProps.push_back(Property(DatumType::String, "Label Format", this, &mLabelFormat));
    outProps.push_back(Property(DatumType::Color, "Highlight Color", this, &mHighlightColor));
    outProps.push_back(Property(DatumType::Float, "Highlight Width", this, &mHighlightWidth));
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

Button* FirstButtonIn(Node* node)
{
    if (node == nullptr) return nullptr;
    if (Button* b = node->As<Button>())
    {
        if (b->IsVisible()) return b;
    }
    for (uint32_t i = 0; i < node->GetNumChildren(); ++i)
    {
        if (Button* b = FirstButtonIn(node->GetChild((int32_t)i))) return b;
    }
    return nullptr;
}
}

Widget* RecompMenuController::Target()
{
    Node* parent = GetParent();
    return parent ? parent->As<Widget>() : nullptr;
}

bool RecompMenuController::TargetVisible()
{
    Widget* target = Target();
    return target != nullptr && target->IsVisible();
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

const std::vector<RecompMenuController*>& RecompMenuController::GetAll()
{
    return Controllers();
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
    if (mToggleButton >= 0 && INP_IsGamepadButtonJustDown(mToggleButton, 0))
    {
        Toggle();
    }

    Widget* target = Target();
    if (!mBoundVariable.empty())
    {
        SyncBoundVariable();
    }
    const bool visible = TargetVisible();
    if (visible && !mWasVisible)
    {
        // select a button once A is up, so the press that opened the UI doesn't press it
        mSelectPending = mCaptureInput;
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
    if (INP_IsGamepadButtonJustDown(GAMEPAD_B, 0) ||
        (!mBoundVariable.empty() && INP_IsGamepadButtonJustDown(GAMEPAD_START, 0)))
    {
        Close();
        return;
    }
    if (mSelectPending && INP_IsGamepadButtonDown(GAMEPAD_A, 0))
    {
        return;
    }
    mSelectPending = false;
    Button* selected = Button::GetSelectedButton();
    if (selected == nullptr || !IsInside(selected, target))
    {
        Button* first = mFirstButton.Get();
        Button::SetSelectedButton(first != nullptr ? first : FirstButtonIn(target));
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
    outProps.push_back(Property(DatumType::Integer, "Toggle Button", this, &mToggleButton));
    outProps.push_back(Property(DatumType::Bool, "In HOME Menu", this, &mInHomeMenu));
    outProps.push_back(Property(DatumType::String, "Bound Variable", this, &mBoundVariable));
}
