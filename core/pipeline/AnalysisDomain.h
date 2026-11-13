#pragma once
#include <QString>

// Both linear domains use linear sRGB/D65 for comparable RGB/Y statistics.
// Source precedes edits; Working ends before output curves/encoding/LUT/grain.
enum class AnalysisDomain { Source, Working, Display };
inline QString analysisDomainKey(AnalysisDomain domain) {
    switch(domain) {
    case AnalysisDomain::Source:return "source";
    case AnalysisDomain::Working:return "working";
    case AnalysisDomain::Display:return "display";
    }
    return {};
}
inline bool decodeAnalysisDomain(const QString &key,AnalysisDomain *output) {
    if(!output)return false;
    if(key=="source")*output=AnalysisDomain::Source;
    else if(key=="working")*output=AnalysisDomain::Working;
    else if(key=="display")*output=AnalysisDomain::Display;
    else return false;
    return true;
}
