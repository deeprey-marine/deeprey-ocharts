#pragma once
#include <wx/wx.h>
#include <functional>
#include <vector>
#include "DpOchartsTypes.h"

class IDpOchartsAPI {
public:
   virtual ~IDpOchartsAPI() = default;
   
   virtual bool Login(const wxString& username, const wxString& password) = 0;
   virtual bool ValidateStoredCredentials(const wxString& username, const wxString& loginKey) = 0;
   virtual void Logout() = 0;
   
   virtual std::vector<DpOchartsChartInfo> GetAvailableCharts() = 0;
   virtual std::vector<DpOchartsChartInfo> GetInstalledCharts() = 0;
   
   using ProgressCallback = std::function<void(int percent)>;
   using CompleteCallback = std::function<void(bool success, const wxString& error)>;
   
   virtual void DownloadChart(const wxString& chartId, 
                             ProgressCallback onProgress,
                             CompleteCallback onComplete) = 0;
   
   virtual bool CancelDownload(const wxString& chartId) = 0;
   virtual bool IsDownloading(const wxString& chartId) = 0;
   virtual wxString GetLastError() const = 0;
   
   virtual void RefreshChartsList() = 0;
   virtual DpOchartsChartInfo GetChartDetails(const wxString& chartId) = 0;
   virtual std::vector<wxString> GetDownloadQueue() = 0;
   virtual bool PauseDownload(const wxString& chartId) = 0;
   virtual bool ResumeDownload(const wxString& chartId) = 0;
   virtual bool IsServiceAvailable() = 0;
   virtual void SyncWithService() = 0;
   virtual wxDateTime GetLastSyncTime() = 0;
};