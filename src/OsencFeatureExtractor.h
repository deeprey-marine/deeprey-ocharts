/******************************************************************************
 * OsencFeatureExtractor.h — turn an installed oeSENC chart set into a routing feature bundle.
 *
 * This is the ONLY place decrypted o-charts geometry is converted for the autorouting
 * compiler. It lives inside the plugin because the plugin is the sole process oexserverd will
 * decrypt for (verified on hardware). The compiler never sees a .oesu; it reads the bundle.
 *
 * What leaves this class is deliberately lossy: depth areas, land, hazards, channels,
 * clearances, TSS, quality zones and coverage — no symbology, text, lights, buoyage or
 * presentation data. The result is a navigable-water skeleton that cannot be rendered as, or
 * converted back into, a chart. See deeprey-api/autoroute/DpRoutingBundle.h for the format.
 *****************************************************************************/
#ifndef OSENC_FEATURE_EXTRACTOR_H
#define OSENC_FEATURE_EXTRACTOR_H

#include <functional>
#include <wx/string.h>

class OsencFeatureExtractor {
public:
    // Extracts every .oesu cell under chartSetDir into a single bundle file.
    // onProgress(done, total) is called per cell so a long extraction can show progress.
    // Returns false and sets GetLastError() on failure; a partial bundle is removed.
    bool Extract(const wxString& chartSetDir, const wxString& bundlePath,
                 std::function<void(int done, int total)> onProgress);

    wxString GetLastError() const { return m_lastError; }

private:
    wxString m_lastError;
};

#endif  // OSENC_FEATURE_EXTRACTOR_H
