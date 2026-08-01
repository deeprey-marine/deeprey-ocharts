/******************************************************************************
 * OsencFeatureExtractor.cpp — see header.
 *
 * Geometry note, because it drives most of what follows: a v200 SENC does NOT store polygon
 * contours. BuildPolyTessGeo() reads only the per-contour vertex COUNTS and then the triangle
 * mesh; the raw contour array is never populated. The boundary survives elsewhere — every area
 * feature also gets SetLineGeometry(..., GEO_AREA, ...) with an edge index table into the
 * cell's shared VE/VC vector tables (Osenc.cpp, FEATURE_GEOMETRY_RECORD_AREA). So areas and
 * lines are reconstructed the same way here, by walking that index table, which yields the
 * original boundary at its original vertex count rather than a 3x-inflated triangle soup.
 *****************************************************************************/
#include "OsencFeatureExtractor.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stream.h>
#include <wx/wfstream.h>

#include "DpRoutingBundle.h"
#include "Osenc.h"
#include "ocpn_plugin.h"
#include "s52s57.h"
#include "s57RegistrarMgr.h"
#include "uKey.h"

extern s57RegistrarMgr* pi_poRegistrarMgr;
extern bool validate_SENC_server(void);

using namespace DpRoutingBundle;

namespace {

// Only these classes ever leave the plugin. Everything else — lights, buoyage, text, symbology,
// presentation — is dropped before it reaches the bundle. The list mirrors what S57Source pulls
// from GDAL for NOAA cells, so the compiler's downstream mapping is shared between the two
// chart sources rather than forked.
bool IsWanted(const char* cls) {
    static const std::map<std::string, bool> k = {
        {"DEPARE", true}, {"DRGARE", true},                       // depth areas
        {"LNDARE", true},                                         // land
        {"OBSTRN", true}, {"UWTROC", true}, {"WRECKS", true},     // hazards
        {"RESARE", true},                                         // restricted
        {"FAIRWY", true}, {"RECTRC", true}, {"DWRTE",  true}, {"NAVLNE", true},
        {"BRIDGE", true}, {"CBLOHD", true}, {"PYLONS", true},     // overhead clearance
        {"TSSLPT", true}, {"TSELNE", true}, {"TSEZNE", true},     // traffic separation
        {"CTNARE", true}, {"ACHARE", true},                       // caution / anchorage
        {"SOUNDG", true},                                         // soundings
        {"M_QUAL", true},                                         // CATZOC quality zones
        {"MARCUL", true},                                         // marine farms (physical)
        {"SLCONS", true}, {"DAMCON", true}, {"CAUSWY", true},     // shoreline constructions
        // CBLSUB / PIPSOL are deliberately absent: they lie on the seabed and must not subtract
        // navigable water.
    };
    return k.find(std::string(cls)) != k.end();
}

// Attributes carried through, in bundle bit order. The f64 values are written in this order,
// skipping any whose bit is clear.
struct AttrMap { const char* acronym; uint16_t bit; };
const AttrMap kAttrs[] = {
    {"DRVAL1", DPRB_ATTR_DRVAL1}, {"DRVAL2", DPRB_ATTR_DRVAL2},
    {"VALSOU", DPRB_ATTR_VALSOU}, {"WATLEV", DPRB_ATTR_WATLEV},
    {"CATREA", DPRB_ATTR_CATREA}, {"RESTRN", DPRB_ATTR_RESTRN},
    {"VERCLR", DPRB_ATTR_VERCLR}, {"HORCLR", DPRB_ATTR_HORCLR},
    {"ORIENT", DPRB_ATTR_ORIENT}, {"CATZOC", DPRB_ATTR_CATZOC},
    {"CATCOV", DPRB_ATTR_CATCOV},
};

// o-charts cell names carry no usage-band digit (unlike NOAA's US5xxxxx), so the band that
// drives finest-wins fusion is derived from the cell's compilation scale, along the S-57 usage
// band boundaries.
uint8_t BandFromScale(int scale) {
    if (scale <= 0)       return 4;      // unknown: approach — neither coarsest nor finest
    if (scale >= 1500000) return 1;      // overview
    if (scale >= 600000)  return 2;      // general
    if (scale >= 150000)  return 3;      // coastal
    if (scale >= 50000)   return 4;      // approach
    if (scale >= 10000)   return 5;      // harbour
    return 6;                            // berthing
}

double AttrAsDouble(const S57attVal* v) {
    if (!v || !v->value) return NAN;
    switch (v->valType) {
        case OGR_INT:  return (double)(*(int*)v->value);
        case OGR_REAL: return *(double*)v->value;
        case OGR_STR: {
            const char* s = (const char*)v->value;
            return (s && *s) ? atof(s) : NAN;   // a list arrives as "7,8": first value wins
        }
        default: return NAN;
    }
}

struct XY { float e, n; };                       // simple-Mercator metres, cell-relative

class BundleWriter {
public:
    explicit BundleWriter(FILE* f) : m_f(f) {}
    void U8(uint8_t v)   { fwrite(&v, 1, 1, m_f); }
    void U16(uint16_t v) { fwrite(&v, 2, 1, m_f); }
    void U32(uint32_t v) { fwrite(&v, 4, 1, m_f); }
    void F32(float v)    { fwrite(&v, 4, 1, m_f); }
    void F64(double v)   { fwrite(&v, 8, 1, m_f); }
    void Str(const char* s) {
        size_t n = strlen(s);
        if (n > 255) n = 255;
        U8((uint8_t)n);
        if (n) fwrite(s, 1, n, m_f);
    }
private:
    FILE* m_f;
};

// Walk an object's edge index table and rebuild its boundary as one or more chains of SM points.
// Layout per entry (mirrors eSENCChart's AssembleLineGeometry): start connected node, edge
// index, end connected node, and — from SENC version 201 — a direction flag. A chain continues
// while each entry's first point coincides with the previous entry's last; otherwise a new chain
// starts, which is how a feature's holes and its multi-part lines both arrive.
void ChainsFromEdges(const S57Obj* obj, int sencVersion,
                     const std::unordered_map<unsigned, VE_Element*>& ve,
                     const std::unordered_map<unsigned, VC_Element*>& vc,
                     std::vector<std::vector<XY> >& chains) {
    const int stride = (sencVersion > 200) ? 4 : 3;
    std::vector<XY> cur;

    for (int iseg = 0; iseg < obj->m_n_lsindex; iseg++) {
        const int* run = &obj->m_lsindex_array[iseg * stride];
        unsigned inode = (unsigned)run[0];
        int venode = run[1];
        unsigned enode = (unsigned)run[2];
        bool forward = true;
        if (stride == 4) {
            forward = (run[3] == 0);
        } else if (venode < 0) {
            venode = -venode;
            forward = false;
        }

        std::vector<XY> seg;
        std::unordered_map<unsigned, VC_Element*>::const_iterator itc = vc.find(inode);
        if (itc != vc.end() && itc->second && itc->second->pPoint) {
            XY p = { itc->second->pPoint[0], itc->second->pPoint[1] };
            seg.push_back(p);
        }

        std::unordered_map<unsigned, VE_Element*>::const_iterator ite = ve.find((unsigned)venode);
        if (ite != ve.end() && ite->second && ite->second->nCount && ite->second->pPoints) {
            const VE_Element* e = ite->second;
            if (forward) {
                for (unsigned k = 0; k < e->nCount; k++) {
                    XY p = { e->pPoints[k * 2], e->pPoints[k * 2 + 1] };
                    seg.push_back(p);
                }
            } else {
                for (int k = (int)e->nCount - 1; k >= 0; k--) {
                    XY p = { e->pPoints[k * 2], e->pPoints[k * 2 + 1] };
                    seg.push_back(p);
                }
            }
        }

        std::unordered_map<unsigned, VC_Element*>::const_iterator ite2 = vc.find(enode);
        if (ite2 != vc.end() && ite2->second && ite2->second->pPoint) {
            XY p = { ite2->second->pPoint[0], ite2->second->pPoint[1] };
            seg.push_back(p);
        }

        if (seg.size() < 2) continue;

        if (cur.empty()) {
            cur.swap(seg);
        } else if (cur.back().e == seg.front().e && cur.back().n == seg.front().n) {
            cur.insert(cur.end(), seg.begin() + 1, seg.end());
        } else {
            chains.push_back(std::vector<XY>());
            chains.back().swap(cur);
            cur.swap(seg);
        }
    }
    if (!cur.empty()) {
        chains.push_back(std::vector<XY>());
        chains.back().swap(cur);
    }
}

// Twice the signed area — the sign gives winding in the SM plane, which is conformal, so it
// matches the winding in lat/lon.
double SignedArea2(const std::vector<XY>& r) {
    double a = 0;
    for (size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        a += ((double)r[j].e * r[i].n) - ((double)r[i].e * r[j].n);
    return a;
}

}  // namespace

bool OsencFeatureExtractor::Extract(const wxString& chartSetDir, const wxString& bundlePath,
                                    std::function<void(int done, int total)> onProgress) {
    m_lastError.Clear();

    if (!pi_poRegistrarMgr) {
        // Without the registrar the SENC's numeric type codes cannot be resolved to S-57
        // acronyms, so every feature would arrive nameless.
        m_lastError = _("Chart dictionary not loaded");
        return false;
    }

    wxArrayString cells;
    wxDir::GetAllFiles(chartSetDir, &cells, _T("*.oesu"), wxDIR_FILES);
    if (cells.IsEmpty()) {
        m_lastError = _("No oeSENC cells found in this chart set");
        return false;
    }

    validate_SENC_server();

    FILE* out = fopen((const char*)bundlePath.mb_str(), "wb");
    if (!out) {
        m_lastError = _("Could not write the routing bundle");
        return false;
    }
    BundleWriter w(out);

    Header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, kMagic, 4);
    hdr.version = kVersion;
    fwrite(&hdr, sizeof(hdr), 1, out);          // rewritten at the end, once the counts are known

    double bla0 = 90, blo0 = 180, bla1 = -90, blo1 = -180;
    uint32_t cellCount = 0, featureCount = 0;
    const int total = (int)cells.GetCount();

    // ---- pass 1: cell index --------------------------------------------------------------
    // ingestHeader() is the only ingest that retains CELL_COVR; ingest200() reads and discards
    // it. Coverage is what the patch planner's gate tests against, so it is captured here.
    for (int i = 0; i < total; i++) {
        const wxString& path = cells[i];
        if (onProgress) onProgress(i, total * 2);

        wxString key = getPrimaryKey(path);
        if (!key.Len()) {
            key = getAlternateKey(path);
            if (!key.Len()) {
                wxLogMessage(_T("OsencFeatureExtractor: no install key for ") + path);
                continue;                       // not licensed on this device — skip quietly
            }
            SwapKeyHashes();
        }

        Osenc senc;
        senc.setRegistrarMgr(pi_poRegistrarMgr);
        senc.setCtype(CTYPE_OESU);
        senc.setKey(key);
        if (senc.ingestHeader(path) != SENC_NO_ERROR) {
            wxLogMessage(_T("OsencFeatureExtractor: header ingest failed for ") + path);
            continue;
        }

        Extent& ext = senc.getReadExtent();
        wxFileName fn(path);
        w.Str((const char*)fn.GetName().mb_str());
        w.U8(BandFromScale(senc.getSENCReadScale()));
        w.F64(ext.SLAT); w.F64(ext.WLON); w.F64(ext.NLAT); w.F64(ext.ELON);

        // Coverage polygons: float lat/lon pairs in degrees, the M_COVR CATCOV=1 boundaries.
        SENCFloatPtrArray& ptrs = senc.getSENCReadAuxPointArray();
        wxArrayInt& cnts = senc.getSENCReadAuxPointCountArray();
        unsigned nring = wxMin(cnts.GetCount(), ptrs.GetCount());
        w.U32(nring);
        for (unsigned j = 0; j < nring; j++) {
            int n = cnts.Item(j);
            float* pf = ptrs.Item(j);
            if (n < 0 || !pf) n = 0;
            w.U32((uint32_t)n);
            for (int k = 0; k < n * 2; k++) w.F32(pf[k]);
        }

        bla0 = wxMin(bla0, ext.SLAT); bla1 = wxMax(bla1, ext.NLAT);
        blo0 = wxMin(blo0, ext.WLON); blo1 = wxMax(blo1, ext.ELON);
        cellCount++;
    }

    // ---- pass 2: features ------------------------------------------------------------------
    for (int i = 0; i < total; i++) {
        const wxString& path = cells[i];
        if (onProgress) onProgress(total + i, total * 2);

        wxString key = getPrimaryKey(path);
        if (!key.Len()) { key = getAlternateKey(path); if (!key.Len()) continue; }

        Osenc senc;
        senc.setRegistrarMgr(pi_poRegistrarMgr);
        senc.setCtype(CTYPE_OESU);
        senc.setKey(key);

        S57ObjVector objs;
        VE_ElementVector ves;
        VC_ElementVector vcs;
        if (senc.ingest200(path, &objs, &ves, &vcs) != SENC_NO_ERROR) {
            wxLogMessage(_T("OsencFeatureExtractor: ingest failed for ") + path);
            continue;
        }

        std::unordered_map<unsigned, VE_Element*> ve;
        std::unordered_map<unsigned, VC_Element*> vc;
        for (size_t k = 0; k < ves.size(); k++) if (ves[k]) ve[ves[k]->index] = ves[k];
        for (size_t k = 0; k < vcs.size(); k++) if (vcs[k]) vc[vcs[k]->index] = vcs[k];

        double refLat = 0, refLon = 0;
        senc.getRefLocn(&refLat, &refLon);
        const uint8_t band = BandFromScale(senc.getSENCReadScale());
        const int sencVersion = senc.getSencReadVersion();

        for (size_t oi = 0; oi < objs.size(); oi++) {
            S57Obj* obj = objs[oi];
            if (!obj || !IsWanted(obj->FeatureName)) continue;

            // ---- attributes ----
            uint16_t mask = 0;
            double vals[16];
            int nvals = 0;
            for (size_t ai = 0; ai < sizeof(kAttrs) / sizeof(kAttrs[0]); ai++) {
                int idx = obj->GetAttributeIndex(kAttrs[ai].acronym);
                if (idx < 0 || !obj->attVal || idx >= (int)obj->attVal->GetCount()) continue;
                double d = AttrAsDouble(obj->attVal->Item(idx));
                if (std::isnan(d)) continue;
                mask |= kAttrs[ai].bit;
                vals[nvals++] = d;
            }

            // ---- geometry ----
            std::vector<std::vector<XY> > chains;      // SM, converted at write time
            std::vector<double> multiLat, multiLon, multiZ;
            uint8_t geom = 0;

            if (obj->Primitive_type == GEO_AREA || obj->Primitive_type == GEO_LINE) {
                if (!obj->m_lsindex_array || obj->m_n_lsindex <= 0) continue;
                ChainsFromEdges(obj, sencVersion, ve, vc, chains);
                if (chains.empty()) continue;

                if (obj->Primitive_type == GEO_AREA) {
                    geom = DPRB_GEOM_AREA;
                    for (size_t k = 0; k < chains.size(); k++) {
                        std::vector<XY>& c = chains[k];
                        if (c.size() < 3) { c.clear(); continue; }
                        if (c.front().e != c.back().e || c.front().n != c.back().n)
                            c.push_back(c.front());     // close what the edge walk left open
                    }
                    // Ring 0 must be the outer boundary, wound CCW, with holes CW: the compiler
                    // unions ENC rings raw under a non-zero fill rule, so a hole that arrived
                    // CCW would fill in solid. The outer ring is the one enclosing the most
                    // area; position in the edge table alone does not guarantee it.
                    size_t outer = 0;
                    double best = -1;
                    for (size_t k = 0; k < chains.size(); k++) {
                        if (chains[k].size() < 4) continue;
                        double a = std::fabs(SignedArea2(chains[k]));
                        if (a > best) { best = a; outer = k; }
                    }
                    if (best < 0) continue;
                    if (outer != 0) chains[0].swap(chains[outer]);
                    for (size_t k = 0; k < chains.size(); k++) {
                        if (chains[k].size() < 4) { chains[k].clear(); continue; }
                        bool isCCW = SignedArea2(chains[k]) > 0;
                        bool wantCCW = (k == 0);
                        if (isCCW != wantCCW)
                            std::reverse(chains[k].begin(), chains[k].end());
                    }
                } else {
                    geom = DPRB_GEOM_LINE;
                    for (size_t k = 0; k < chains.size(); k++)
                        if (chains[k].size() < 2) chains[k].clear();
                }
            } else if (obj->Primitive_type == GEO_POINT && obj->geoPtMulti && obj->npt > 1) {
                geom = DPRB_GEOM_MULTIPOINT;         // soundings
                for (int k = 0; k < obj->npt; k++) {
                    multiLon.push_back(obj->geoPtMulti[k * 2]);
                    multiLat.push_back(obj->geoPtMulti[k * 2 + 1]);
                    multiZ.push_back(obj->geoPtz ? obj->geoPtz[k * 3 + 2] : NAN);
                }
            } else if (obj->Primitive_type == GEO_POINT) {
                geom = DPRB_GEOM_POINT;
            } else {
                continue;                            // GEO_META and friends carry no routing data
            }

            // ---- emit ----
            size_t nrings = 0;
            if (geom == DPRB_GEOM_MULTIPOINT) nrings = 2;      // positions, then depths
            else if (geom == DPRB_GEOM_POINT) nrings = 1;
            else for (size_t k = 0; k < chains.size(); k++) if (!chains[k].empty()) nrings++;
            if (!nrings) continue;

            w.Str(obj->FeatureName);
            w.U8(geom);
            w.U8(band);
            w.U16(mask);
            for (int v = 0; v < nvals; v++) w.F64(vals[v]);
            w.U32((uint32_t)nrings);

            if (geom == DPRB_GEOM_MULTIPOINT) {
                w.U32((uint32_t)multiLat.size());
                for (size_t k = 0; k < multiLat.size(); k++) {
                    w.F32((float)multiLat[k]); w.F32((float)multiLon[k]);
                }
                // Depths ride in a parallel ring; the second slot of each pair is unused.
                w.U32((uint32_t)multiZ.size());
                for (size_t k = 0; k < multiZ.size(); k++) { w.F32((float)multiZ[k]); w.F32(0.f); }
            } else if (geom == DPRB_GEOM_POINT) {
                w.U32(1);
                w.F32((float)obj->m_lat); w.F32((float)obj->m_lon);
            } else {
                for (size_t k = 0; k < chains.size(); k++) {
                    const std::vector<XY>& c = chains[k];
                    if (c.empty()) continue;
                    w.U32((uint32_t)c.size());
                    for (size_t q = 0; q < c.size(); q++) {
                        double lat, lon;
                        fromSM_Plugin(c[q].e, c[q].n, refLat, refLon, &lat, &lon);
                        w.F32((float)lat); w.F32((float)lon);
                    }
                }
            }
            featureCount++;
        }

        for (size_t k = 0; k < objs.size(); k++) delete objs[k];
        for (size_t k = 0; k < ves.size(); k++) if (ves[k]) { free(ves[k]->pPoints); delete ves[k]; }
        for (size_t k = 0; k < vcs.size(); k++) if (vcs[k]) { free(vcs[k]->pPoint); delete vcs[k]; }
    }

    if (onProgress) onProgress(total * 2, total * 2);

    hdr.cell_count = cellCount;
    hdr.feature_count = featureCount;
    hdr.la0 = bla0; hdr.lo0 = blo0; hdr.la1 = bla1; hdr.lo1 = blo1;
    fseek(out, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, out);
    fclose(out);

    if (cellCount == 0 || featureCount == 0) {
        ::wxRemoveFile(bundlePath);
        m_lastError = _("This chart set yielded no routing data");
        return false;
    }

    wxLogMessage(wxString::Format(_T("OsencFeatureExtractor: %u cells, %u features -> %s"),
                                  cellCount, featureCount, bundlePath.c_str()));
    return true;
}
