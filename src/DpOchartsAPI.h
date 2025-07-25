#pragma once
#include "IDpOchartsAPI.h"
class shopPanel;

class DpOchartsAPI : public IDpOchartsAPI
{
public:
    DpOchartsAPI();
    void SetShopPanel(shopPanel* shoppanel);
    virtual bool Login(const wxString& username, const wxString& password, wxString& loginKey);
    virtual bool ValidateStoredCredentials(const wxString& username, const wxString& loginKey);
    virtual void Logout();

    virtual std::vector<DpOchartsChartInfo> GetAvailableCharts();
    virtual std::vector<DpOchartsChartInfo> GetInstalledCharts();

    using ProgressCallback = std::function<void(int percent)>;
    using CompleteCallback = std::function<void(bool success, const wxString& error)>;

    virtual void DownloadChart(const wxString& chartId,
        ProgressCallback onProgress,
        CompleteCallback onComplete);

    virtual bool CancelDownload(const wxString& chartId);
    virtual bool IsDownloading(const wxString& chartId);
    virtual wxString GetLastError() const;

    virtual void RefreshChartsList();
    virtual DpOchartsChartInfo GetChartDetails(const wxString& chartId);
    virtual std::vector<wxString> GetDownloadQueue();
    virtual bool PauseDownload(const wxString& chartId);
    virtual bool ResumeDownload(const wxString& chartId);
    virtual bool IsServiceAvailable();
    virtual void SyncWithService();
    virtual wxDateTime GetLastSyncTime();
private:
    std::vector<DpOchartsChartInfo> GetCharts();
    wxString m_lastError;
    shopPanel* m_shoppanel;
};
