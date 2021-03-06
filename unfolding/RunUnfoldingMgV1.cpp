#ifndef __CLING__
#include <iostream>
#include <memory>
#include <vector>
#include <RStringView.h>
#include <ROOT/TSeq.hxx>
#include <TFile.h>
#include <TKey.h>
#include <TRandom.h>
#include <TTree.h>
#include <TTreeReader.h>
#endif

#include "../helpers/string.C"
#include "unfoldingGeneral.cpp"

std::vector<double> MakePtBinningSmeared(std::string_view trigger) {
  std::vector<double> binlimits;
  if(contains(trigger, "INT7")){
    std::cout << "Using binning for trigger INT7\n";
    binlimits = {20, 30, 40, 50, 60, 80, 100, 120};
  } else if(contains(trigger, "EJ2")){
    std::cout << "Using binning for trigger EJ2\n";
    binlimits = {60, 70, 80, 100, 120, 140, 160};
  } else if(contains(trigger, "EJ1")){
    std::cout << "Using binning for trigger EJ1\n";
    binlimits = {80, 90, 100, 110, 120, 140, 160, 180, 200, 220, 240, 260};
  }
  return binlimits;
}

TTree *GetDataTree(TFile &reader) {
  TTree *result(nullptr);
  for(auto k : TRangeDynCast<TKey>(gDirectory->GetListOfKeys())){
    if(!k) continue;
    if((contains(k->GetName(), "JetSubstructure") || contains(k->GetName(), "jetSubstructure"))
       && (k->ReadObj()->IsA() == TTree::Class())) {
      result = dynamic_cast<TTree *>(k->ReadObj());
    }
  }
  std::cout << "Found tree with name " << result->GetName() << std::endl;
  return result;
}

void RunUnfoldingMgV1(const std::string_view filedata, const std::string_view filemc, double fracSmearClosure = 0.2)
{
  auto ptbinvec_smear = MakePtBinningSmeared(filedata); // Smeared binnning - only in the region one trusts the data
  std::vector<double> ptbinvec_true;
  for(auto f = 0.; f <= 400.; f+= 20.) ptbinvec_true.emplace_back(f);
  // zg must range from 0 to 0.5
  std::vector<double> massbins;
  for(auto f = 0.; f <= 50.; f+= 0.5) massbins.emplace_back(f);

  auto mydataextractor = [](const std::string_view filename, double ptminsmear, double ptmaxsmear, TH2 *hraw){
    std::unique_ptr<TFile> datafilereader(TFile::Open(filename.data(), "READ"));
    TTreeReader datareader(GetDataTree(*datafilereader));
    TTreeReaderValue<double>  ptrecData(datareader, "PtJetRec"), 
                              massRecData(datareader, "MgMeasured");
    for(auto en : datareader){
      if(*ptrecData < ptminsmear || *ptrecData > ptmaxsmear) continue;
      hraw->Fill(*massRecData, *ptrecData);
    }
  };
  auto mymcextractor = [fracSmearClosure](const std::string_view filename, double ptminsmear, double ptmaxsmear, TH2 *h2true, TH2 *h2smeared, TH2 *h2smearedClosure, TH2 *h2smearednocuts, TH2 *h2fulleff, RooUnfoldResponse &response, RooUnfoldResponse &responsenotrunc, RooUnfoldResponse &responseClosure){
  std::unique_ptr<TFile> mcfilereader(TFile::Open(filename.data(), "READ"));
    TTreeReader mcreader(GetDataTree(*mcfilereader));
    TTreeReaderValue<double>  ptrec(mcreader, "PtJetRec"), 
                              ptsim(mcreader, "PtJetSim"), 
                              massRec(mcreader, "MgMeasured"), 
                              massSim(mcreader, "MgTrue"),
                              weight(mcreader, "PythiaWeight");
    TRandom samplesplitter;
    for(auto en : mcreader){
      //if(*ptsim > 200.) continue;
      h2fulleff->Fill(*massSim, *ptsim, *weight);
      h2smearednocuts->Fill(*massRec, *ptrec, *weight);
      responsenotrunc.Fill(*massRec, *ptrec, *massSim, *ptsim, *weight);

      // apply reconstruction level cuts
      if(*ptrec > ptmaxsmear || *ptrec < ptminsmear) continue;
      h2smeared->Fill(*massRec, *ptrec, *weight);
      h2true->Fill(*massSim, *ptsim, *weight);
      response.Fill(*massRec, *ptrec, *massSim, *ptsim, *weight);
      
      // split sample for closure test
      // test sample and response must be statistically independent
      // Split size determined by fraction used for smeared histogram
      auto test = samplesplitter.Uniform();
      if(test < fracSmearClosure) {
        h2smearedClosure->Fill(*massRec, *ptrec, *weight);
      } else {
        responseClosure.Fill(*massRec, *ptrec, *massSim, *ptsim, *weight);
      }
    }
  };
  unfoldingGeneral("Mg", filedata, filemc, {ptbinvec_true, massbins, ptbinvec_smear, massbins}, mydataextractor, mymcextractor);
}