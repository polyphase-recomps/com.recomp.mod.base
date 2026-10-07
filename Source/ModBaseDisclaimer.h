/**
 * @file ModBaseDisclaimer.h
 * @brief The disclaimer scene: pages the player accepts before the game's first scene.
 *
 * Made by Tools > Recomp > Mods > Disclaimer (ModDisclaimer_Generate, from the Mod Map's
 * ModDisclaimerSettings). A RecompDisclaimer node runs it:
 * - the first time, each page waits for Accept (a RecompButton with Setting
 *   "@disclaimer:accept"); Decline ("@disclaimer:decline") quits a packaged game;
 * - once all are accepted, that is saved (<save name>.agreed: the project's Saves folder in the
 *   editor, the game's save storage when packaged) with a fingerprint of the pages' text;
 * - later runs show each page for Hold Seconds (A / Start skips it) and go on to Next Scene. A
 *   change to the text asks again.
 * The page's text goes to the Title / Progress / Text texts of its UI.
 */
#pragma once

#include "ModBaseApi.h"

#include "AssetRef.h"
#include "Nodes/Widgets/Widget.h"

#include <string>

class ModMap;
class Node;

class MODBASE_API RecompDisclaimer : public Widget
{
public:
    DECLARE_NODE(RecompDisclaimer, Widget);

    static constexpr int kMaxPages = 4;

    virtual void Start() override;
    virtual void Stop() override;
    virtual void Destroy() override;
    virtual void Tick(float deltaTime) override;
    virtual void EditorTick(float deltaTime) override;
    virtual void GatherProperties(std::vector<Property>& outProps) override;

    void Accept();
    void Decline();

    void SetPage(int index, const std::string& title, const std::string& text);
    void SetPageCount(int count);
    void SetNextScene(const AssetRef& scene);
    void SetHoldSeconds(float seconds);
    void SetSaveName(const std::string& name);
    // Shows a page (the editor's preview: the first).
    void ShowPage(int index);

    // The disclaimer whose UI holds `from` (else the first running one).
    static RecompDisclaimer* Find(Node* from = nullptr);

protected:
    std::string Fingerprint() const;
    bool WasAccepted() const;
    void SaveAccepted() const;
    void Finish();
    void FitText();

    std::string mTitles[kMaxPages];
    std::string mTexts[kMaxPages];
    int32_t mPageCount = 2;
    AssetRef mNextScene;
    float mHoldSeconds = 2.0f;
    std::string mSaveName = "recomp";

    int32_t mPage = 0;
    bool mAccepted = false; // earlier: the pages only show for a moment
    float mTimer = 0.0f;
    bool mDone = false;
};

// "@disclaimer:accept" / "@disclaimer:decline" (RecompButton Setting).
MODBASE_API bool RecompDisclaimer_Command(const std::string& command, Node* from);

// The map's look (menu style + disclaimer settings) over a disclaimer UI.
MODBASE_API void ModDisclaimer_ApplyLook(Node* root, const ModMap& map);
