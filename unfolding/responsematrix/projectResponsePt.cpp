#ifndef __CLING__
#include "ROOT/TSeq.hxx"
#include "RStringView.h"
#include <TCanvas.h>
#include <TArrayD.h>
#include <TFile.h>
#include <TH2.h>
#endif

#include "../../helpers/msl.C"

void projectResponsePt(const std::string_view inputfile, const std::string_view observable, int binpttrue, int binptmeasured){  
  TH2 *responseMatrix2D(nullptr), *hraw(nullptr), *htrue(nullptr);
  {
    std::unique_ptr<TFile> reader(TFile::Open(inputfile.data(), "READ"));
    responseMatrix2D = static_cast<TH2 *>(reader->Get("ResponseMatrix2D"));
    responseMatrix2D->SetDirectory(nullptr);
    hraw = static_cast<TH2 *>(reader->Get("hraw"));
    hraw->SetDirectory(nullptr);
    htrue = static_cast<TH2 *>(reader->Get("true"));
    htrue->SetDirectory(nullptr);
  }

  auto projected = sliceResponsePtBase(responseMatrix2D, htrue, hraw, observable.data(), binpttrue, binptmeasured);
  projected->SetDirectory(nullptr);
  projected->SetStats(false);
  projected->SetXTitle("p_{t} (GeV/c), measured");
  projected->SetYTitle("p_{t} (GeV/c), true");
  
  auto plot = new TCanvas(Form("response_slicept_binpt%d", binpttrue), Form("Response matrix sliced in zg for pt-true bin %d", binpttrue), 800, 600);
  plot->cd();
  projected->Draw("colz");
}