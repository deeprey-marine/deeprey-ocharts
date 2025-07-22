#include "DpOchartsAPI.h"

int doLogin( /*wxWindow *parent*/ wxString dpLogin, wxString dpPass);
extern wxString g_dpMessage;
bool DpOchartsAPI::Login(const wxString& username, const wxString& password) {
    g_dpMessage = wxEmptyString;
    bool ok = doLogin(username, password) == 1;
    m_lastError = g_dpMessage.Trim(false);
    return ok;
}
bool DpOchartsAPI::ValidateStoredCredentials(const wxString& username, const wxString& loginKey){ return loginKey.Len() != 0; }
void DpOchartsAPI::Logout(){ }

std::vector<DpOchartsChartInfo> DpOchartsAPI::GetAvailableCharts() { return std::vector<DpOchartsChartInfo>(); }
std::vector<DpOchartsChartInfo> DpOchartsAPI::GetInstalledCharts(){ return std::vector<DpOchartsChartInfo>(); }

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
