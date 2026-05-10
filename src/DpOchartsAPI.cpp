#include "DpOchartsAPI.h"
#include "ochartShop.h"
#include "ocpn_plugin.h"
#include "fpr.h"
#include <wx/fileconf.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <map>
#include <thread>

int doLogin( /*wxWindow *parent*/ wxString dpLogin, wxString dpPass);
extern wxString g_dpMessage;
extern std::vector<itemChart*> ChartVector;
extern wxString                        g_loginUser;
extern wxString                        g_loginKey;
extern wxString                        g_systemName;
extern wxString                        g_dongleName;
extern bool                            g_chartListUpdatedOK;
extern itemChart* gtargetChart;
extern shopPanel *g_shopPanel;

extern std::function<void(int percent)> g_dpDownloadProgressCallback;
extern std::function<void(bool success, const wxString& error)> g_dpDownloadCompleteCallback;

extern bool g_benableRebuild;
extern wxString g_PrivateDataDir;  // ~/.opencpn/o_charts_pi/, set in o-charts_pi.cpp
extern wxFileConfig *g_pconfig;     // OpenCPN's config object, set in o-charts_pi.cpp
void saveShopConfig();

extern wxArrayString g_systemNameChoiceArray;
extern wxArrayString g_systemNameServerArray;
extern wxArrayString g_systemNameDisabledArray;

DpOchartsAPI::DpOchartsAPI() :m_shoppanel(nullptr), m_hiddenFrame(nullptr) {}
void DpOchartsAPI::SetShopPanel(shopPanel* shoppanel) { m_shoppanel = shoppanel; }

shopPanel* DpOchartsAPI::EnsureShopPanel() {
    if(m_shoppanel)
        return m_shoppanel;
    if(g_shopPanel)
        return g_shopPanel;

    // Create a hidden shopPanel for headless API use
    if(!m_hiddenFrame)
        m_hiddenFrame = new wxFrame(GetOCPNCanvasWindow(), wxID_ANY, _T(""), wxDefaultPosition, wxSize(1,1), wxFRAME_NO_TASKBAR);
    m_shoppanel = new shopPanel(m_hiddenFrame, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_shoppanel->Hide();
    return m_shoppanel;
}
bool DpOchartsAPI::Login(const wxString& username, const wxString& password, wxString& loginKey) {
    g_dpMessage = wxEmptyString;
    bool ok = doLogin(username, password) == 1;
    if (ok) loginKey = g_loginKey;
    m_lastError = g_dpMessage.Trim(false);
    return ok;
}
bool DpOchartsAPI::ValidateStoredCredentials(const wxString& username, const wxString& loginKey){
    g_loginUser = username;
    g_loginKey = loginKey;
    return !username.empty() && !loginKey.empty();
}
void DpOchartsAPI::Logout() {
    // Abort any in-flight download; reuses curl-thread cancellation.
    if (shopPanel* panel = EnsureShopPanel())
        panel->OnButtonCancelOp();

    // Sever download callbacks before freeing chart data they may reference.
    g_dpDownloadProgressCallback = nullptr;
    g_dpDownloadCompleteCallback = nullptr;

    // gtargetChart is a raw pointer into ChartVector; null it before deleting items.
    gtargetChart = nullptr;
    for (itemChart* c : ChartVector) delete c;
    ChartVector.clear();

    // Session globals.
    g_loginUser.Clear();
    g_loginKey.Clear();
    g_systemName.Clear();
    g_dongleName.Clear();
    g_dpMessage.Clear();
    g_chartListUpdatedOK = false;

    // System-name lists are repopulated by the next getChartList/ProcessResponse.
    g_systemNameChoiceArray.Clear();
    g_systemNameServerArray.Clear();
    g_systemNameDisabledArray.Clear();

    m_lastError.Clear();

    // Wipe the persisted login key only; loadShopConfig() reads this on startup
    // and would otherwise silently re-authenticate the previous user.
    if (wxFileConfig* pConf = GetOCPNConfigObject()) {
        pConf->SetPath(_T("/PlugIns/ocharts"));
        pConf->DeleteEntry(_T("loginKey"));
        pConf->Flush();
    }
}

std::vector<DpOchartsChartInfo> DpOchartsAPI::GetAvailableCharts() {
    return GetCharts();
}

std::vector<DpOchartsChartInfo> DpOchartsAPI::GetCharts() {
    g_dpMessage = wxEmptyString;
    shopPanel::OnButtonUpdate();
    m_lastError = g_dpMessage.Trim(false);
    return ConvertChartVector();
}

std::vector<DpOchartsChartInfo> DpOchartsAPI::ConvertChartVector() {
    std::vector<DpOchartsChartInfo> result;
    for (itemChart* chart : ChartVector)
    {
        DpOchartsChartInfo dpChart;

        dpChart.id = chart->chartID;
        dpChart.orderRef = chart->orderRef;
        dpChart.name = chart->chartName;
        dpChart.version = chart->serverChartEdition;
        int status = chart->getChartStatus();
        itemSlot* slot = chart->GetActiveSlot();

        // STAT_EXPIRED short-circuits getChartStatus() before m_assignedSlotIndex
        // is set, so GetActiveSlot() returns null and the previously-installed
        // edition would be lost. Recover it for this device only.
        if (!slot && status == STAT_EXPIRED) {
            int qId = -1;
            int slotIdx = chart->GetSlotAssignedToInstalledDongle(qId);
            if (slotIdx < 0) slotIdx = chart->GetSlotAssignedToSystem(qId);
            if (slotIdx >= 0) {
                int qtyIdx = chart->FindQuantityIndex(qId);
                if (qtyIdx >= 0 &&
                    slotIdx < (int)chart->quantityList[qtyIdx].slotList.size()) {
                    slot = chart->quantityList[qtyIdx].slotList[slotIdx];
                }
            }
        }

        dpChart.installedVersion = slot ? wxString(slot->installedEdition) : wxString();
        wxString::const_iterator dummy;
        dpChart.expiryDate.ParseFormat(chart->expDate, "%Y-%m-%d %H:%M:%S", &dummy);

        static const std::map<int, DpChartStatus> statusToDpStatus = {
            { STAT_EXPIRED, DpChartStatus::EXPIRED },
            { STAT_PURCHASED_NOSLOT, DpChartStatus::FULLY_ASSIGNED },
            { STAT_PURCHASED, DpChartStatus::AVAILABLE },
            { STAT_REQUESTABLE, DpChartStatus::AVAILABLE },
            { STAT_CURRENT, DpChartStatus::INSTALLED },
            { STAT_STALE, DpChartStatus::UPDATE_AVAILABLE }
        };
        auto it = statusToDpStatus.find(status);
        dpChart.status = it == statusToDpStatus.end() ? DpChartStatus::CHART_ERROR : it->second;
        dpChart.slotsTotal = (int)(chart->quantityList.size() * chart->maxSlots);
        dpChart.sizeBytes = 0;

        if (slot)
        {
            for (itemTaskFileInfo* fileInfo : slot->taskFileList)
            {
                wxString fileSizeStr(fileInfo->fileSize);
                unsigned long long fileSizeULL;
                if (fileSizeStr.ToULongLong(&fileSizeULL)) dpChart.sizeBytes += fileSizeULL;
            }
        }
        dpChart.description = chart->chartName;
        dpChart.region = chart->chartID;
        dpChart.thumbnailPath = wxEmptyString;
        wxString editionDateStr(chart->editionDate);
        unsigned long long editionDateULL;
        if (editionDateStr.ToULongLong(&editionDateULL))
            dpChart.lastModified = wxDateTime((time_t)editionDateULL);
        dpChart.downloadPercent = 0;
        dpChart.previewBitmap = chart->GetChartThumbnail(200, true);

        for (itemQuantity& Qty: chart->quantityList) {
            for (itemSlot* slot : Qty.slotList) {
                wxString assignment = chart->getKeytypeString(slot->slotUuid) +
                    _T("    ") + wxString(slot->assignedSystemName.c_str());
                dpChart.assigments.push_back(assignment);
            }
        }

        result.push_back(dpChart);
    }
    return result;
}

// Async chart-list fetch.
//
// The legacy shopPanel::OnButtonUpdate() can't run as a whole on a worker
// thread: it calls IsDongleAvailable() which uses wxExecute() (not thread-safe
// — wxExecute touches GTK signal handlers and wxAppTraits in the main loop)
// and GetShopNameFromFPR() which can pop an error dialog. We split it:
//
//   1. Main thread: dongle / FPR detection (fast, <100ms typically).
//   2. Worker thread: getChartList() — the actual ~10s HTTP POST. This path is
//      pure libcurl + data parsing; checkResult/checkResponseCode have all
//      their wxMessageBox calls commented out, so it's thread-safe.
//   3. Main thread (via wxTheApp->CallAfter): ConvertChartVector() builds
//      wxBitmap thumbnails (not thread-safe), then invoke caller's callback.
extern int getChartList(bool bShowErrorDialogs);

void DpOchartsAPI::GetAvailableChartsAsync(ChartsCallback onComplete) {
    g_dpMessage = wxEmptyString;

    // Step 1 — main thread: replicate the prep that shopPanel::OnButtonUpdate
    // does before the network call. We can't call OnButtonUpdate() itself from
    // a worker thread because of IsDongleAvailable()/wxExecute().
    if (g_loginKey.Len() == 0) {
        // Not authenticated — nothing to fetch.
        if (onComplete) onComplete({});
        return;
    }

    g_dongleName.Clear();
    if (IsDongleAvailable()) {
        g_dongleName = shopPanel::GetDongleName();
    } else if (!g_systemName.Length()) {
        shopPanel::GetShopNameFromFPR();
    }

    // Step 2 — worker thread: HTTP POST.
    std::thread([this, onComplete]() {
        int err_code = getChartList(false);
        g_chartListUpdatedOK = (err_code == 0);

        // Step 3 — back on UI thread: build DpOchartsChartInfo (with wxBitmaps)
        // and invoke the caller.
        wxTheApp->CallAfter([this, onComplete]() {
            m_lastError = g_dpMessage.Trim(false);
            std::vector<DpOchartsChartInfo> charts = ConvertChartVector();
            if (onComplete) onComplete(charts);
        });
    }).detach();
}

std::vector<DpOchartsChartInfo> DpOchartsAPI::GetInstalledCharts(){
    std::vector<DpOchartsChartInfo> charts = GetCharts();
    std::vector<DpOchartsChartInfo> result;
    for (DpOchartsChartInfo chart : charts)
        if (chart.status == DpChartStatus::INSTALLED) result.push_back(chart);
    return result;
}

// Pick the installable ChartVector entry for a given chartID.
//
// chartID identifies the chart product, not an individual order. A user who
// has bought the same product twice (e.g. last year's edition + this year's)
// ends up with two ChartVector entries sharing the chartID. A naive
// first-match loop will pick whichever the server returned first, which is
// typically the older fully-assigned order — install then fails with
// "no free slot" even though the user clicked the new order's card.
//
// Preference order:
//   1. Already assigned to this MFD's dongle / systemName (re-install path).
//   2. Has at least one free slot in some quantity (new assignment can succeed).
//   3. Fallback: first match (download proceeds and fails downstream with a
//      meaningful error).
static itemChart* selectChartForOp(const wxString& chartId, bool requireAssignedHere)
{
    itemChart* assigned = nullptr;
    itemChart* withFreeSlot = nullptr;
    itemChart* firstMatch = nullptr;
    int matchCount = 0;

    for (itemChart* c : ChartVector) {
        if (!c || c->chartID != chartId) continue;
        matchCount++;
        if (!firstMatch) firstMatch = c;

        bool isAssignedHere = false;
        if (g_dongleName.Length() && c->isChartsetAssignedToSystemKey(g_dongleName))
            isAssignedHere = true;
        if (!isAssignedHere && g_systemName.Length() &&
            c->isChartsetAssignedToSystemKey(g_systemName))
            isAssignedHere = true;
        if (isAssignedHere) {
            assigned = c;
            break;
        }

        if (!withFreeSlot) {
            for (itemQuantity& Q : c->quantityList) {
                unsigned int realAssigned = 0;
                for (itemSlot* s : Q.slotList) {
                    if (s && (strlen(s->slotUuid.c_str()) ||
                              strlen(s->assignedSystemName.c_str())))
                        realAssigned++;
                }
                if (realAssigned < c->maxSlots) {
                    withFreeSlot = c;
                    break;
                }
            }
        }
    }

    itemChart* chosen =
        assigned ? assigned : (requireAssignedHere ? nullptr :
                              (withFreeSlot ? withFreeSlot : firstMatch));

    if (matchCount > 1) {
        wxLogMessage(_T("o-charts_pi: chartID=%s has %d entries; chose orderRef=%s reason=%s"),
            wxString(chartId), matchCount,
            chosen ? wxString(chosen->orderRef) : wxString("<none>"),
            assigned ? _T("assigned-to-this-device") :
            (withFreeSlot && chosen == withFreeSlot ? _T("has-free-slot") :
            (chosen ? _T("fallback-first-match") : _T("none-eligible"))));
        wxLog::FlushActive();
    }
    return chosen;
}

void DpOchartsAPI::DownloadChart(const wxString& chartId,
    ProgressCallback onProgress,
    CompleteCallback onComplete){
    shopPanel* panel = EnsureShopPanel();
    if(!panel){
        if(onComplete) onComplete(false, _("Shop panel not available"));
        return;
    }

    itemChart* chart = selectChartForOp(chartId, /*requireAssignedHere=*/false);
    if (!chart) {
        if (onComplete) onComplete(false, _("Chart not found"));
        return;
    }

    g_dpDownloadProgressCallback = onProgress;
    g_dpDownloadCompleteCallback = onComplete;
    g_dpMessage = wxEmptyString;
    panel->OnButtonInstall(chart);
}

bool DpOchartsAPI::CancelDownload(const wxString& chartId){
    shopPanel* panel = EnsureShopPanel();
    if(!panel) return false;

    // Match against ChartVector instead of dereferencing gtargetChart: the
    // global is null until doDownload runs, and stale across sessions if a
    // prior install flow returned before doDownload was reached. Touching it
    // unconditionally segfaults on the cancel-while-stuck path.
    itemChart* chart = nullptr;
    for (itemChart* c : ChartVector) {
        if (c->chartID == chartId) { chart = c; break; }
    }
    if (!chart) return false;

    panel->OnButtonCancelOp();
    return true;
}
void DpOchartsAPI::UninstallChart(const wxString& chartId, UninstallCallback onComplete) {
    g_dpMessage = wxEmptyString;
    m_lastError.Clear();

    // Same disambiguation as DownloadChart: with two orders for one chartID,
    // the entry assigned to this device is the only correct one to uninstall.
    itemChart* chart = selectChartForOp(chartId, /*requireAssignedHere=*/true);
    if (!chart) {
        if (onComplete) onComplete(false, _("Chart not installed on this device"));
        return;
    }

    if (chart->m_downloading ||
        (gtargetChart && gtargetChart->chartID == chart->chartID)) {
        if (onComplete) onComplete(false, _("Download in progress — cancel it first"));
        return;
    }

    int qId = -1;
    int slotIdx = chart->GetSlotAssignedToInstalledDongle(qId);
    if (slotIdx < 0) slotIdx = chart->GetSlotAssignedToSystem(qId);
    if (slotIdx < 0) {
        if (onComplete) onComplete(false, _("No slot assigned to this device"));
        return;
    }
    int qtyIdx = chart->FindQuantityIndex(qId);
    if (qtyIdx < 0 || slotIdx >= (int)chart->quantityList[qtyIdx].slotList.size()) {
        if (onComplete) onComplete(false, _("Slot lookup failed"));
        return;
    }
    itemSlot* slot = chart->quantityList[qtyIdx].slotList[slotIdx];
    if (!slot) {
        if (onComplete) onComplete(false, _("Slot lookup failed"));
        return;
    }

    if (slot->installedEdition.empty() && slot->chartDirName.empty()) {
        if (onComplete) onComplete(false, _("Chart is not installed on this device"));
        return;
    }

    // Snapshot paths derived from slot state BEFORE we mutate it.
    wxString installParent = wxString(slot->installLocation.c_str());
    wxString installDir;
    if (!installParent.IsEmpty() && !slot->chartDirName.empty()) {
        installDir = installParent +
                     wxFileName::GetPathSeparator() +
                     wxString(slot->chartDirName.c_str());
    }
    wxString prefix = (chart->GetChartType() == CHART_TYPE_OEUSENC) ? "oeuSENC" : "oeRNC";
    wxString chartIdStr(chart->chartID.c_str());
    wxString editionYear = wxString(slot->installedEdition.c_str()).BeforeFirst('/');

    // 1) Wipe install directory for this slot. Other slots / chartsets under
    // the same installLocation parent are untouched.
    if (!installDir.IsEmpty() && wxDir::Exists(installDir)) {
        if (!wxFileName::Rmdir(installDir, wxPATH_RMDIR_RECURSIVE)) {
            wxLogMessage("o-charts uninstall: partial Rmdir on %s", installDir);
        }
    }

    // 2) Wipe the DownloadCache directory for this edition. Path formula
    // matches doDownload() at ochartShop.cpp:3613-3616 and scrubCache() at
    // ochartShop.cpp:5331-5411.
    wxString cacheRoot = g_PrivateDataDir + "DownloadCache" + wxFileName::GetPathSeparator();
    if (!editionYear.IsEmpty()) {
        wxString cacheDir = cacheRoot + prefix + "-" + chartIdStr + "-" + editionYear;
        if (wxDir::Exists(cacheDir)) {
            wxFileName::Rmdir(cacheDir, wxPATH_RMDIR_RECURSIVE);
        }
    } else {
        // Slot state is partial — glob "<prefix>-<id>-*" and remove any match.
        wxDir dcd(cacheRoot);
        if (dcd.IsOpened()) {
            wxString name;
            wxString pattern = prefix + "-" + chartIdStr + "-*";
            bool cont = dcd.GetFirst(&name, pattern, wxDIR_DIRS);
            while (cont) {
                wxFileName::Rmdir(cacheRoot + name, wxPATH_RMDIR_RECURSIVE);
                cont = dcd.GetNext(&name);
            }
        }
    }

    // 3) Clear local-install state. The slot remains assigned to this device
    // server-side (assignedSystemName preserved), so re-download skips
    // reassignment. Next getChartStatus() will report STAT_REQUESTABLE
    // (or STAT_EXPIRED if applicable).
    slot->installedEdition.clear();
    slot->installLocation.clear();
    slot->chartDirName.clear();

    saveShopConfig();

    // 4) Force OpenCPN to rescan and rewrite chartlist.dat. Symmetric to
    // install at ochartShop.cpp:6476-6480. ForceChartDBUpdate() alone is a
    // no-op outside the open Options dialog (see ocpn_plugin_gui.cpp:1403).
    //
    // Two pitfalls solved here:
    //   a) ChartDatabase::Update() at chartdbs.cpp:1811 REPLACES m_dir_array
    //      with what we pass, and the result is persisted into chartlist.dat
    //      (loaded back as m_dir_array on next OpenCPN start). So whatever we
    //      pass is sticky across sessions until the next rebuild.
    //   b) The install path's "covered" check at ochartShop.cpp:6427 uses
    //      StartsWith() against m_dir_array. If a parent dir like
    //      "/home/opencpn/Charts" sits in there, EVERY future install path
    //      under it tests as covered, install skips its own
    //      UpdateChartDBInplace, and the new charts never make it into
    //      chartlist.dat — so they're invisible despite being on disk.
    //
    // Avoid both by building dirs from scratch:
    //   - persistent ChartDirs straight from opencpn.conf (g_pconfig), not
    //     GetChartDBDirArrayString() — that would inherit any prior pollution
    //   - plus the per-chartset install dir of every still-installed slot
    //     (the just-removed slot's state was cleared above so it's excluded)
    wxArrayString dirs;
    if (g_pconfig) {
        wxString savedPath = g_pconfig->GetPath();
        g_pconfig->SetPath("/ChartDirectories");
        wxString key, value;
        long index;
        bool cont = g_pconfig->GetFirstEntry(key, index);
        while (cont) {
            if (g_pconfig->Read(key, &value) && !value.IsEmpty()) {
                dirs.Add(value);
            }
            cont = g_pconfig->GetNextEntry(key, index);
        }
        g_pconfig->SetPath(savedPath);
    }
    for (size_t i = 0; i < ChartVector.size(); i++) {
        itemChart* c = ChartVector[i];
        if (!c) continue;
        for (size_t qi = 0; qi < c->quantityList.size(); qi++) {
            for (size_t si = 0; si < c->quantityList[qi].slotList.size(); si++) {
                itemSlot* s = c->quantityList[qi].slotList[si];
                if (!s || s->installLocation.empty() || s->chartDirName.empty())
                    continue;
                wxString chartDir = wxString(s->installLocation.c_str()) +
                                    wxFileName::GetPathSeparator() +
                                    wxString(s->chartDirName.c_str());
                if (dirs.Index(chartDir) == wxNOT_FOUND)
                    dirs.Add(chartDir);
            }
        }
    }
    UpdateChartDBInplace(dirs, /*b_force_update=*/true, /*b_ProgressDialog=*/false);

    if (onComplete) onComplete(true, wxEmptyString);
}

bool DpOchartsAPI::IsDownloading(const wxString& chartId){ return false; }
wxString DpOchartsAPI::GetLastError() const { return m_lastError; }

void DpOchartsAPI::RefreshChartsList(){ }
DpOchartsChartInfo DpOchartsAPI::GetChartDetails(const wxString& chartId) { return DpOchartsChartInfo(); }
std::vector<wxString> DpOchartsAPI::GetDownloadQueue() { return std::vector<wxString>(); }
bool DpOchartsAPI::PauseDownload(const wxString& chartId){ return false; }
bool DpOchartsAPI::ResumeDownload(const wxString& chartId){ return false; }
bool DpOchartsAPI::IsServiceAvailable(){ return false; }
void DpOchartsAPI::SyncWithService(){ }
wxDateTime DpOchartsAPI::GetLastSyncTime() { return wxDateTime(); }
wxString DpOchartsAPI::GetSystemName() { return g_systemName; }
wxString DpOchartsAPI::GetDongleName() { return g_dongleName; }
