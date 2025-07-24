#include "DpOchartsAPI.h"
#include "ochartShop.h"
#include <map>

int doLogin( /*wxWindow *parent*/ wxString dpLogin, wxString dpPass);
extern wxString g_dpMessage;
extern std::vector<itemChart*> ChartVector;
bool DpOchartsAPI::Login(const wxString& username, const wxString& password) {
    g_dpMessage = wxEmptyString;
    bool ok = doLogin(username, password) == 1;
    m_lastError = g_dpMessage.Trim(false);
    return ok;
}
bool DpOchartsAPI::ValidateStoredCredentials(const wxString& username, const wxString& loginKey){ return loginKey.Len() != 0; }
void DpOchartsAPI::Logout(){ }

std::vector<DpOchartsChartInfo> DpOchartsAPI::GetAvailableCharts() {
    return GetCharts();
}

std::vector<DpOchartsChartInfo> DpOchartsAPI::GetCharts() {
    g_dpMessage = wxEmptyString;
    bool ok = shopPanel::OnButtonUpdate();
    m_lastError = g_dpMessage.Trim(false);

    std::vector<DpOchartsChartInfo> result;
    for (itemChart* chart : ChartVector)
    {
        DpOchartsChartInfo dpChart;

        dpChart.id = chart->chartID;
        dpChart.name = chart->chartName;
        dpChart.version = chart->serverChartEdition;
        dpChart.installedVersion = chart->GetActiveSlot()->installedEdition;        
        wxString::const_iterator dummy;
        dpChart.expiryDate.ParseFormat(chart->expDate, &dummy);
        int status = chart->getChartStatus();
        static const std::map<int, DpChartStatus> statusToDpStatus = {
            { STAT_EXPIRED, DpChartStatus::EXPIRED },
            { STAT_PURCHASED_NOSLOT, DpChartStatus::INSTALLED },
            { STAT_PURCHASED, DpChartStatus::INSTALLED },
            { STAT_REQUESTABLE, DpChartStatus::AVAILABLE },
            { STAT_CURRENT, DpChartStatus::INSTALLED },
            { STAT_STALE, DpChartStatus::UPDATE_AVAILABLE }
        };
        auto it = statusToDpStatus.find(status);
        dpChart.status = it == statusToDpStatus.end() ? DpChartStatus::CHART_ERROR : it->second;
        dpChart.sizeBytes = 0;
        itemSlot* slot = chart->GetActiveSlot();
        for (itemTaskFileInfo* fileInfo : slot->taskFileList)
        {
            wxString fileSizeStr(fileInfo->fileSize);
            unsigned long long fileSizeULL;
            if (fileSizeStr.ToULongLong(&fileSizeULL)) dpChart.sizeBytes += fileSizeULL;
        }
        dpChart.description = chart->chartName;
        dpChart.region = chart->chartName;
        dpChart.thumbnailPath = wxEmptyString; // what is this for ?
        dpChart.lastModified.ParseFormat(chart->editionDate, &dummy);
        dpChart.downloadPercent = 0;

        result.push_back(dpChart);
    }
    return result;
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
    CompleteCallback onComplete){ }

bool DpOchartsAPI::CancelDownload(const wxString& chartId){ return false; }
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
