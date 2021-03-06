#ifndef __CLING__
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <ROOT/TSeq.hxx>
#include <RStringView.h>
#include <TCanvas.h>
#include <TFile.h>
#include <TH1.h>
#include <TKey.h>
#include <TLegend.h>
#include <TPaveText.h>

#include "TAxisFrame.h"
#include "TDefaultLegend.h"
#include "TNDCLabel.h"
#include "TSavableCanvas.h"
#endif

#include "../../helpers/graphics.C"

std::string findDirectory(const TFile &reader, const std::string_view jettype, const std::string_view trigger){
  std::string result;
  for(auto d : *(gDirectory->GetListOfKeys())){
    std::string_view keyname = d->GetName();
    if(keyname.find(jettype) == std::string::npos) continue;
    if(keyname.find(trigger) == std::string::npos) continue;
    result = std::string(keyname); 
  }
  return result;
}

std::vector<TH1 *> readTriggerHistos(const std::string_view filename, const std::string_view jettype, const std::string_view trigger){
  std::vector<TH1 *> result;
  std::unique_ptr<TFile> reader(TFile::Open(filename.data(), "READ"));
  auto dirname = findDirectory(*reader, jettype, trigger);
  if(!dirname.length()) return result;
  reader->cd(dirname.data());
  for(auto k : *(gDirectory->GetListOfKeys())){
    if(std::string_view(k->GetName()).find("JetSpectrum") == std::string::npos) continue;
    if(std::string_view(k->GetName()).find("Raw") != std::string::npos) continue;
    auto h = static_cast<TH1 *>(static_cast<TKey *>(k)->ReadObj()); 
    h->SetDirectory(nullptr);
    result.emplace_back(h);
  }
  return result;
}

std::vector<TH1 *> readTriggerHistosNorm(const std::string_view filename, const std::string_view jettype, const std::string_view trigger, const std::string_view triggercluster){
  std::vector<TH1 *> result;
  std::unique_ptr<TFile> reader(TFile::Open(filename.data(), "READ"));
  auto dirname = findDirectory(*reader, jettype, trigger);
  if(!dirname.length()) return result;
  reader->cd(dirname.data());

  for(auto radius : ROOT::TSeqI(2, 6)){
    std::stringstream eventsname, rawname, normalizedname;
    eventsname << "EventCount_" << jettype << "_R" << std::setw(2) << std::setfill('0') << radius << "_" << trigger;
    rawname << "RawJetSpectrum_" << jettype << "_R" << std::setw(2) << std::setfill('0') << radius << "_" << trigger << "_" << triggercluster;
    normalizedname << "JetSpectrum_" << jettype << "_R" << std::setw(2) << std::setfill('0') << radius << "_" << trigger << "_" << triggercluster;
    std::cout << "Getting event counter " << eventsname.str() << ", raw spectrum " << rawname.str().data() << std::endl;
    auto ev = static_cast<TH1 *>(gDirectory->Get(eventsname.str().data())),
         spectrum = static_cast<TH1 *>(gDirectory->Get(rawname.str().data())); 
    spectrum->SetDirectory(nullptr);
    spectrum->SetName(normalizedname.str().data());
    spectrum->Scale(1./ev->GetBinContent(1));
    result.emplace_back(spectrum);
  }
  return result;
}

void makePeriodComparison1617(const std::string_view jettype, const std::string_view trigger, const std::string_view triggercluster){
  std::map<std::string, Style> periods = {
    {"LHC16h", {kOrange, 29}}, {"LHC16i", {kBlue, 25}}, {"LHC16j", {kGreen, 26}}, {"LHC16k", {kViolet, 27}}, {"LHC17", {kRed, 28}}
  };

  auto plot = new ROOT6tools::TSavableCanvas(Form("periodComparison%s%s", jettype.data(), trigger.data()), Form("Period comparison %s in trigger %s", jettype.data(), trigger.data()), 1200, 1000);
  plot->Divide(2,2);

  int ipad = 1;
  TLegend *leg(nullptr);
  double xrange = (trigger == std::string_view("INT7") ? 100. : 200.), yrange = (trigger == std::string_view("INT7") ? 1e-9 : 1e-7);
  for(auto irad : ROOT::TSeqI(2,6)){
    plot->cd(ipad);
    gPad->SetLogy();
    (new ROOT6tools::TAxisFrame(Form("JetSpecFrameR%02d%s%s", irad, jettype.data(), trigger.data()), "p_{t,jet} (GeV/c)", "1/N_{trg} dN_{jet}/dp_{t,jet} ((GeV/c)^{-1}", 0., xrange, yrange, 200))->Draw("axis");

    if(ipad == 1) {
      leg = new ROOT6tools::TDefaultLegend(0.65, 0.45, 0.89, 0.89);
      leg->Draw();

      (new ROOT6tools::TNDCLabel(0.15, 0.8, 0.5, 0.88, Form("%s, trigger %s", jettype.data(), trigger.data())))->Draw();
    }

    (new ROOT6tools::TNDCLabel(0.15, 0.72, 0.25, 0.79, Form("R=%.1f", double(irad)/10.)))->Draw();
    ipad++;
  }

  for(const auto &p : periods){
    std::stringstream infilename;
    infilename << p.first << "/JetSpectra_" << triggercluster << ".root";
    auto histos = (p.first.find("LHC16") != std::string::npos) ? readTriggerHistos(infilename.str(), jettype, trigger) :readTriggerHistosNorm(infilename.str(), jettype, trigger, triggercluster) ;
    
    ipad = 1;
    for(auto irad : ROOT::TSeqI(2, 6)){
      plot->cd(ipad);
      auto hist = *std::find_if(histos.begin(), histos.end(), [irad](const TH1 *hist) -> bool { return std::string(hist->GetName()).find(Form("R%02d", irad)) != std::string::npos; });
      p.second.SetStyle<TH1>(*hist);
      hist->Draw("epsame");
      if(ipad == 1) leg->AddEntry(hist, p.first.data(), "lep");
      ipad++;
    }
  }

  plot->cd();
  plot->Update();
  plot->SaveCanvas(plot->GetName());
}