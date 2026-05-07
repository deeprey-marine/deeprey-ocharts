#include "DpOchartsAPI.h"
#include "ochartShop.h"
#include "ocpn_plugin.h"
#include "fpr.h"
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
void DpOchartsAPI::Logout(){ }

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
        dpChart.previewBitmap = chart->GetChartThumbnail(100, true);

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

void DpOchartsAPI::DownloadChart(const wxString& chartId,
    ProgressCallback onProgress,
    CompleteCallback onComplete){
    shopPanel* panel = EnsureShopPanel();
    if(!panel){
        if(onComplete) onComplete(false, _("Shop panel not available"));
        return;
    }
    for (itemChart* chart : ChartVector)
    {
        if (chart->chartID == chartId)
        {
            g_dpDownloadProgressCallback = onProgress;
            g_dpDownloadCompleteCallback = onComplete;
            g_dpMessage = wxEmptyString;
            panel->OnButtonInstall(chart);
            break;
        }
    }
}

bool DpOchartsAPI::CancelDownload(const wxString& chartId){
    shopPanel* panel = EnsureShopPanel();
    if(!panel) return false;
    if (gtargetChart->chartID == chartId)
    {
        panel->OnButtonCancelOp();
        return true;
    }
    else
    {
        return false;
    }
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
