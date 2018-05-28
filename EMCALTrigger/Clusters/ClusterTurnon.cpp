#ifndef __CLING__
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <RStringView.h>
#include <ROOT/TSeq.hxx>
#include <TF1.h>
#include <TFile.h>
#include <TH1.h>
#include <TKey.h>

#include <TAxisFrame.h>
#include <TDefaultLegend.h>
#include <TGraphicsStyle.h>
#include <TNDCLabel.h>
#include <TSavableCanvas.h>
#endif

#include "../../helpers/string.C"

using gooditer = std::vector<int>::iterator;

struct Specname
{
  std::string tag;
  std::string detector;
  std::string trigger;
  std::string cluster;
};

std::map<TString, ROOT6tools::TGraphicsStyle> trgstyle = {
    {"MB", ROOT6tools::TGraphicsStyle(kBlack, 20)},
    //    {"MC7", ROOT6tools::TGraphicsStyle(kRed, 24)},
    {"G1", ROOT6tools::TGraphicsStyle(kOrange, 25)},
    {"G2", ROOT6tools::TGraphicsStyle(kViolet, 26)},
    {"J1", ROOT6tools::TGraphicsStyle(kBlue, 27)},
    {"J2", ROOT6tools::TGraphicsStyle(kGreen, 28)}};

Specname DecodeSpectrumName(const std::string_view specname)
{
  std::cout << "decoding " << specname << std::endl;
  auto tokens = tokenize(std::string(specname), '_');
  return {tokens[0], tokens[1], tokens[2], tokens[3]};
}

std::vector<TH1 *> ReadTriggers(const char *filename)
{
  std::vector<TH1 *> result;
  std::unique_ptr<TFile> reader(TFile::Open(filename, "READ"));
  reader->cd("ANY");
  gDirectory->ls();
  for (auto spec : *(gDirectory->GetListOfKeys()))
  {
    auto specname = DecodeSpectrumName(spec->GetName());
    auto hist = static_cast<TH1 *>(static_cast<TKey *>(spec)->ReadObj());
    hist->SetDirectory(nullptr);

    if (specname.trigger == "MB")
    {
      std::stringstream histname;
      histname << (specname.detector == "EMCAL" ? "E" : "D") << specname.trigger;
      hist->SetName(histname.str().data());
    }
    else
    {
      hist->SetName(specname.trigger.data());
    }
    std::cout << "Adding histogram with name" << hist->GetName() << std::endl;
    result.emplace_back(hist);
  }
  return result;
}

TH1 *Histfinder(const std::vector<TH1 *> &data, const std::string_view histname)
{
  for (auto d : data)
    std::cout << d->GetName() << std::endl;
  auto result = std::find_if(data.begin(), data.end(), [&histname](TH1 *test) -> bool { 
    return TString(test->GetName()) == TString(histname.data());
  });
  return result != data.end() ? *result : nullptr;
}

ROOT6tools::TSavableCanvas *MakeTurnonPlots(const std::vector<TH1 *> &data)
{
  //std::array<std::vector<std::string>, 2> triggers = {{{{"MC7", "G1", "G2"}}, {{"J1", "J2"}}}};
  std::array<std::array<std::string, 2>, 2> triggers = {{{{"G1", "G2"}}, {{"J1", "J2"}}}};
  std::array<std::string, 2> detectors = {"E", "D"};
  std::array<double, 4> ymax = {15000., 15000., 30000., 120000.};

  ROOT6tools::TSavableCanvas *turnonCanvas = new ROOT6tools::TSavableCanvas("turnonPlot", "Turnon curves", 1200, 1000);
  turnonCanvas->Divide(2, 2);

  int padcount = 1;
  for (auto d : detectors)
  {
    TH1 *mbref = Histfinder(data, d + "MB");
    for (auto t : triggers)
    {
      turnonCanvas->cd(padcount++);
      gPad->SetLeftMargin(0.14);
      gPad->SetRightMargin(0.04);
      gPad->SetTopMargin(0.04);
      TH1 *turnonframe = new ROOT6tools::TAxisFrame(Form("turnonAxis%d", padcount - 1), "E_{cluster} (GeV)", "Trigger / Min. Bias", 0., 100., 0., ymax[padcount - 2]);
      turnonframe->GetYaxis()->SetTitleOffset(1.7);
      turnonframe->Draw("axis");
      TLegend *leg = new ROOT6tools::TDefaultLegend(0.55, 0.72, 0.93, 0.93);
      leg->Draw();
      // for the first pad per detector label trigger
      if (!(padcount % 2))
        (new ROOT6tools::TNDCLabel(0.18, 0.85, 0.3, 0.92, d == "E" ? "EMCAL" : "DCAL"))->Draw();
      for (auto s : t)
      {
        TH1 *trgspec = static_cast<TH1 *>(Histfinder(data, d + s)->Clone(std::string("Turnon" + d + s).data()));
        trgspec->SetDirectory(nullptr);
        trgspec->Divide(mbref);
        const ROOT6tools::TGraphicsStyle &mystyle = trgstyle[s];
        mystyle.DefineHistogram(trgspec);
        trgspec->Draw("epsame");

        bool isJet = s.find("J") != std::string::npos;
        TF1 *turnonfit = new TF1(std::string("Fit" + d + s).data(), "pol0", isJet ? 20. : 10, 100);
        trgspec->Fit(turnonfit, "N", "", isJet ? 20 : 10, isJet ? 100 : 50);
        turnonfit->SetLineColor(mystyle.GetColor());
        turnonfit->SetLineStyle(2);
        turnonfit->Draw("lsame");

        leg->AddEntry(trgspec, Form("%s: %.1f #pm %.1f", std::string(d + s).data(), turnonfit->GetParameter(0), turnonfit->GetParError(0)), "lep");
      }
    }
  }
  turnonCanvas->cd();

  return turnonCanvas;
}

ROOT6tools::TSavableCanvas *MakeSpectraPlot(const std::vector<TH1 *> &data)
{
  //std::array<std::string, 6> triggers = {"MB", "MC7", "G1", "G2", "J1", "J2"};
  std::array<std::string, 5> triggers = {"MB", "G1", "G2", "J1", "J2"};
  std::array<std::string, 2> detectors = {"E", "D"};
  ROOT6tools::TSavableCanvas *specplot = new ROOT6tools::TSavableCanvas("specplot", "Triggered spectra", 1200, 600);
  specplot->Divide(2, 1);

  int padcount = 1;
  for (auto d : detectors)
  {
    specplot->cd(padcount++);
    gPad->SetLogy();
    gPad->SetLeftMargin(0.12);
    gPad->SetRightMargin(0.04);
    gPad->SetTopMargin(0.04);
    TH1 *specframe = new ROOT6tools::TAxisFrame(std::string("specframe" + d).data(), "E_{cluster}", "1/N_{ev} dN/dE_{cluster} (GeV^{-1})", 0., 100., 1e-9, 100.);
    specframe->GetYaxis()->SetTitleOffset(1.7);
    specframe->Draw("axis");
    (new ROOT6tools::TNDCLabel(0.14, 0.12, 0.25, 0.16, d == "E" ? "EMCAL" : "DCAL"))->Draw();
    TLegend *leg = new ROOT6tools::TDefaultLegend(0.77, 0.68, 0.93, 0.93);
    leg->Draw();
    for (auto t : triggers)
    {
      TH1 *spec = static_cast<TH1 *>(Histfinder(data, d + t)->Clone(std::string("Spec" + d + t).data()));
      const ROOT6tools::TGraphicsStyle &mystyle = trgstyle[t];
      mystyle.DefineHistogram(spec);
      spec->Draw("epsame");
      leg->AddEntry(spec, t == "MB" ? t.data() : std::string(d + t).data(), "lep");
    }
    gPad->Update();
  }
  specplot->cd();
  return specplot;
}

void ClusterTurnon(const char *filename = "ClusterSpectra.root")
{
  auto data = ReadTriggers(filename);
  MakeSpectraPlot(data)->SaveCanvas("ClusterSpectra");
  MakeTurnonPlots(data)->SaveCanvas("ClusterTurnon");
}