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
#include <wx/arrstr.h>
#include <wx/string.h>

// Resumable by design. Decryption must happen on the thread that owns the plugin's globals —
// the key hashes in uKey.cpp are swapped by the chart RENDER path with no lock, and
// validate_SENC_server() calls wxExecute — so this cannot simply be moved to a worker. Instead
// the caller drives it a few cells at a time from its own event loop, which keeps the display
// alive, makes the read abandonable, and leaves every one of those globals single-threaded.
class OsencFeatureExtractor {
public:
    OsencFeatureExtractor();
    ~OsencFeatureExtractor();

    // One-shot form: Begin/Step/Finish driven to completion internally. onProgress(done, total)
    // is called per cell. Blocks until the whole chart set is read.
    bool Extract(const wxString& chartSetDir, const wxString& bundlePath,
                 std::function<void(int done, int total)> onProgress);

    // Chunked form. Begin() enumerates the cells and opens the bundle; Step() advances by at
    // most maxCells and returns true while work remains; Finish() completes the bundle.
    // Destroying the object mid-read abandons it and removes the partial bundle.
    bool Begin(const wxString& chartSetDir, const wxString& bundlePath);
    bool Step(int maxCells);
    bool Finish();

    int Done() const;
    int Total() const;      // two passes over the cells: index, then features

    wxString GetLastError() const { return m_lastError; }

private:
    void RunHeaderChunk(int from, int to);
    void RunFeatureChunk(int from, int to);
    void Cleanup(bool removeBundle);

    struct State;           // cells, bundle handle, writer, counters — all in the .cpp
    State* m_state = nullptr;
    wxString m_lastError;
};

#endif  // OSENC_FEATURE_EXTRACTOR_H
