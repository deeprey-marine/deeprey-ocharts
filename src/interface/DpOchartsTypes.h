#pragma once
#include <wx/wx.h>
#include <wx/datetime.h>
//#include "DpTheme.h"

enum class DpChartStatus {
   AVAILABLE,
   DOWNLOADING,
   QUEUED,
   PAUSED,
   VERIFYING,
   EXTRACTING,
   INSTALLED,
   UPDATE_AVAILABLE,
   EXPIRED,
   CHART_ERROR
};

struct DpOchartsChartInfo {
   wxString id;
   wxString name;
   wxString version;
   wxString installedVersion;
   wxDateTime expiryDate;
   DpChartStatus status;
   size_t sizeBytes;
   wxString description;
   wxString region;
   wxString thumbnailPath;
   wxDateTime lastModified;
   int downloadPercent;
   wxBitmap previewBitmap;  
   
   bool IsInstalled() const { 
       return status == DpChartStatus::INSTALLED || 
              status == DpChartStatus::UPDATE_AVAILABLE ||
              status == DpChartStatus::EXPIRED;
   }
   
   bool CanDownload() const {
       return status == DpChartStatus::AVAILABLE || 
              status == DpChartStatus::UPDATE_AVAILABLE ||
              status == DpChartStatus::PAUSED;
   }
   
   bool IsDownloading() const {
       return status == DpChartStatus::DOWNLOADING ||
              status == DpChartStatus::QUEUED ||
              status == DpChartStatus::VERIFYING ||
              status == DpChartStatus::EXTRACTING;
   }
};

struct DpOchartsCredentials {
   wxString username;
   wxString loginKey;
   wxString systemName;
};