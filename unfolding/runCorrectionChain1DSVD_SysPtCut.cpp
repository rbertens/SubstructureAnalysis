#include "../meta/stl.C"
#include "../meta/root.C"
#include "../meta/roounfold.C"
#include "../helpers/math.C"
#include "../helpers/root.C"
#include "../meta/root6tools.C"
#include "../helpers/graphics.C"
#include "../helpers/pthard.C"
#include "../helpers/substructuretree.C"
#include "../helpers/unfolding.C"
#include "binnings/binningPt1D.C"

std::vector<std::string> triggers = {"INT7", "EJ1"};

TH1 *readSmeared(const std::string_view inputfile, bool weighted, bool downscaleweighted, bool dooutlierrejection){
    auto binning = getJetPtBinningNonLinSmearLarge();
    ROOT::RDataFrame df(GetNameJetSubstructureTree(inputfile), inputfile);
    TH1 *result(nullptr);
    if(weighted){
        if(dooutlierrejection){
            auto hist = df.Filter([](double ptsim, int ptbin) { return !IsOutlierFast(ptsim, ptbin); },{"PtJetSim", "PtHardBin"}).Histo1D({"spectrum", "spectrum", static_cast<int>(binning.size()-1), binning.data()}, "PtJetRec", "PythiaWeight");
            result = histcopy(hist.GetPtr());
        } else {
            auto hist = df.Histo1D({"spectrum", "spectrum", static_cast<int>(binning.size()-1), binning.data()}, "PtJetRec", "PythiaWeight");
            result = histcopy(hist.GetPtr());
        }
    } else if(downscaleweighted){
        // data - no outlier rejection
        auto hist = df.Histo1D({"spectrum", "spectrum", static_cast<int>(binning.size()-1), binning.data()}, "PtJetRec", "EventWeight");
        result = histcopy(hist.GetPtr());
    } else {
        if(dooutlierrejection) {
            auto hist = df.Filter([](double ptsim, int ptbin) { return !IsOutlierFast(ptsim, ptbin); },{"PtJetSim", "PtHardBin"}).Histo1D({"spectrum", "spectrum", static_cast<int>(binning.size()-1), binning.data()}, "PtJetRec");
            result = histcopy(hist.GetPtr());
        } else {
            auto hist = df.Histo1D({"spectrum", "spectrum", static_cast<int>(binning.size()-1), binning.data()}, "PtJetRec");
            result = histcopy(hist.GetPtr());
        }
    }
    result->SetDirectory(nullptr);
    return result;
}

std::vector<TH1 *> extractCENTNOTRDCorrection(std::string_view filename){
    auto binning = getJetPtBinningNonLinSmearLarge();
    ROOT::RDataFrame df(GetNameJetSubstructureTree(filename), filename);
    TH1 *result(nullptr);
    auto selCENT = df.Filter("TriggerClusterIndex < 1");
    auto speccentnotrd = df.Histo1D({"speccentnotrd", "Spectrum centnotrd", static_cast<int>(binning.size()) - 1, binning.data()}, "PtJetRec"),
         speccent = selCENT.Histo1D({"speccent", "Spectrum cent", static_cast<int>(binning.size()) - 1, binning.data()}, "PtJetRec");
    speccent->Sumw2();
    speccentnotrd->Sumw2();
    auto correction = histcopy(speccentnotrd.GetPtr());
    correction->SetNameTitle("CENTNOTRDCorrection", "Correction for the unmeasured CENTNOTRD Luminosity");
    correction->SetDirectory(nullptr);
    correction->Divide(speccentnotrd.GetPtr(), speccent.GetPtr(), 1., 1., "b");
    auto rescentnotrd = histcopy(speccentnotrd.GetPtr()), rescent = histcopy(speccent.GetPtr());
    rescentnotrd->SetDirectory(nullptr);
    rescent->SetDirectory(nullptr);
    return {rescentnotrd, rescent, correction};
}

double extractCENTNOTRDCorrectionFromClusterCounter(const std::string_view filename, double radius, double ptcut) {
    std::unique_ptr<TFile> reader(TFile::Open(filename.data(), "READ"));
    reader->cd(Form("JetSubstructure_FullJets_R%02d_EJ1_pt%d", int(radius*10.), int(ptcut)));
    auto histlist = static_cast<TKey *>(gDirectory->GetListOfKeys()->At(0))->ReadObject<TList>();
    auto clustercounter = static_cast<TH1 *>(histlist->FindObject("hTriggerClusterCounter"));
    if(!clustercounter) return -1.;             // not found
    auto centpluscentnotrdcounter = clustercounter->GetBinContent(clustercounter->FindBin(0)),
         onlycentnotrdcounter = clustercounter->GetBinContent(clustercounter->FindBin(2));
    return (centpluscentnotrdcounter + onlycentnotrdcounter) / centpluscentnotrdcounter;
}

double extractLumiCENT(const std::string_view filename, double ptcut){
    std::pair<double, double> result;
    std::unique_ptr<TFile> reader(TFile::Open(filename.data(), "READ"));
    reader->cd(Form("JetSubstructure_FullJets_R02_INT7_pt%d", int(ptcut)));
    auto list = static_cast<TKey *>(gDirectory->GetListOfKeys()->At(0))->ReadObject<TList>();
    auto lumihist = static_cast<TH1 *>(list->FindObject("hLumiMonitor"));
    auto lumicent = lumihist->GetBinContent(lumihist->GetXaxis()->FindBin("CENT"));
    return lumicent;
}

std::map<std::string, int> readNriggers(double ptcut, const std::string_view filename = "data/merged_1617/AnalysisResults_split.root"){
    std::unique_ptr<TFile> reader(TFile::Open(filename.data(), "READ"));
    std::map<std::string, int> nevents;
    for(auto trg : triggers) {
        std::stringstream dirname;
        dirname << "JetSubstructure_FullJets_R02_" << trg << "_pt" << int(ptcut);
        reader->cd(dirname.str().data());
        auto list = static_cast<TKey *>(gDirectory->GetListOfKeys()->At(0))->ReadObject<TList>();
        auto evhist = static_cast<TH1 *>(list->FindObject("hEventCounter"));
        nevents[trg] = evhist->GetBinContent(1);
    }    
    return nevents;
}

std::vector<std::string> getSortedKeys(const std::map<std::string, std::vector<TObject *>> &data) {
    std::vector<std::string> keys;
    for(const auto &k : data){
        keys.push_back(k.first);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

void runCorrectionChain1DSVD_SysPtCut(double radius, double ptcut, const std::string_view indatadir = ""){
    std::string datadir;
    if (indatadir.length()) datadir = std::string(indatadir);
    else datadir = gSystem->GetWorkingDirectory();
    std::cout << "[SVD unfolding] Using data directory " << datadir << std::endl;
    std::cout << "[SVD unfolding] Reading luminosity for cluster CENT " << std::endl;
    std::string normfilename = Form("%s/data/merged_17/AnalysisResults_split.root", datadir.data());
    auto lumiCENT = extractLumiCENT(normfilename.data(), ptcut);
    std::cout << "[SVD unfolding] Getting correction factor for CENTNOTRD cluster" << std::endl;
    std::vector<TH1 *> centnotrdCorrection;
    double cntcorrectionvalue = extractCENTNOTRDCorrectionFromClusterCounter(normfilename.data(), radius, ptcut);
    if(cntcorrectionvalue < 0){
        // counter historgam not found (old output) - try with jet spectra
        std::cout << "[SVD unfolding] Getting CENTNOTRD correction from spectra comparison (old method)" << std::endl;
        centnotrdCorrection = extractCENTNOTRDCorrection(Form("%s/data/merged_17/JetSubstructureTree_FullJets_R%02d_EJ1_pt%d.root", datadir.data(), int(radius*10.), int(ptcut)));
        TF1 fit("centnotrdcorrfit", "pol0", 0., 200.);
        centnotrdCorrection[2]->Fit(&fit, "N", "", 20., 200.);
        cntcorrectionvalue = fit.GetParameter(0);
    } else {
        std::cout << "[SVD unfolding] CENTNOTRD correction was obtained from trigger cluster counter (new method)" << std::endl;
    }
    std::cout << "[SVD unfolding] Using CENTNOTRD correction factor " << cntcorrectionvalue << std::endl;
    auto lumiCENTNOTRD = lumiCENT * cntcorrectionvalue;
    auto lumihist = new TH1D("luminosities", "Luminosities", 3, 0., 3.);
    lumihist->SetDirectory(nullptr);
    lumihist->GetXaxis()->SetBinLabel(1, "INT7");
    lumihist->GetXaxis()->SetBinLabel(2, "CENT");
    lumihist->GetXaxis()->SetBinLabel(3, "CENTNOTRD");
    lumihist->SetBinContent(2, lumiCENT);
    lumihist->SetBinContent(3, lumiCENTNOTRD);
    std::map<std::string, TH1 *> mcspectra, dataspectra;
    // Read MC specta
    std::cout << "[SVD unfolding] Reading Monte-Carlo spectra for trigger efficiency correction" << std::endl;
    for(const auto &trg : triggers) {
        std::stringstream filename;
        filename << datadir << "/mc/merged_calo/JetSubstructureTree_FullJets_R" << std::setw(2) << std::setfill('0') << int(radius*10.) << "_" << trg << "_pt" << int(ptcut) << "_merged.root";
        auto spec = readSmeared(filename.str(), true, false, true);
        spec->SetName(Form("mcspec_R%02d_%s", int(radius*10.), trg.data()));
        mcspectra[trg] = spec;
    }
    // Read data specta
    std::cout << "[SVD unfolding] Reading data spectra for all triggers" << std::endl;
    for(const auto &trg : triggers) {
        std::stringstream filename;
        filename << datadir << "/data/" << (trg == "INT7" ? "merged_1617" : "merged_17") << "/JetSubstructureTree_FullJets_R" << std::setw(2) << std::setfill('0') << int(radius*10.) << "_" << trg << "_pt" << int(ptcut) << ".root";
        auto spec = readSmeared(filename.str(), false, trg == "EJ2", false);
        spec->SetName(Form("dataspec_R%02d_%s", int(radius*10.), trg.data()));
        dataspectra[trg] = spec;
    }
    // get weights and renormalize data spectra
    std::cout << "[SVD unfolding] normalize spectra" << std::endl;
    auto weights = readNriggers(ptcut, Form("%s/data/merged_1617/AnalysisResults_split.root", datadir.data()));
    std::map<std::string, TH1 *> hnorm;
    for(auto &spec : dataspectra) {
        auto trgweight = weights.find(spec.first)->second;
        if(spec.first == "EJ1") {
            spec.second->Scale(1./lumiCENTNOTRD);
        } else if(spec.first == "EJ2") {
            spec.second->Scale(1./lumiCENT);
        } else {
            lumihist->SetBinContent(1, trgweight);
            spec.second->Scale(1./trgweight);
        }
        auto normhist = new TH1D(Form("norm%s", spec.first.data()), Form("event count trigger %s", spec.first.data()), 1, 0.5, 1.5);
        normhist->SetBinContent(1, trgweight);
        hnorm[spec.first] = normhist;
    }
    // build efficiencies, correct triggered spectra
    std::cout << "[SVD unfolding] Building trigger efficiency" << std::endl;
    std::map<std::string, TH1 *> efficiencies;
    auto reference = mcspectra.find("INT7")->second;
    for(auto &trg : triggers) {
        if(trg == "INT7") continue;
        auto eff = histcopy(mcspectra.find(trg)->second);
        eff->SetName(Form("Efficiency_R%02d_%s", int(radius*10.), trg.data()));
        eff->Divide(eff, reference, 1., 1., "b");
        efficiencies[trg] = eff;
        auto tocorrect = dataspectra.find(trg)->second;
        tocorrect->Divide(eff);
    }

    // ratio trigger / min bias
    std::map<std::string, TH1 *> ratios;
    auto dataref = dataspectra.find("INT7")->second;
    for(auto &trg : triggers){
        if(trg == "INT7") continue;
        auto ratio = histcopy(dataspectra.find(trg)->second);
        ratio->SetName(Form("%soverMB_R%02d", trg.data(), int(radius*10)));
        ratio->Divide(dataref);
        ratios[trg] = ratio;
    }

    // combine jet spectrum in data (for unfolding)
    auto hraw = histcopy(dataspectra.find("INT7")->second);
    hraw->SetNameTitle("hraw", "raw spectrum from various triggers");
    auto triggered = dataspectra.find("EJ1")->second;
    for(auto b : ROOT::TSeqI(0, hraw->GetNbinsX())){
        if(hraw->GetXaxis()->GetBinCenter(b+1) < 70.) continue;       // Use data from INT7 trigger
        // else Use data from EJ1 trigger
        hraw->SetBinContent(b+1, triggered->GetBinContent(b+1));
        hraw->SetBinError(b+1, triggered->GetBinError(b+1));
    }
    std::cout << "[SVD unfolding] Raw spectrum ready, getting detector response ..." << std::endl;

    // read MC
    auto binningdet = getJetPtBinningNonLinSmearLarge(), 
         binningpart = getJetPtBinningNonLinTrueLarge();
    auto ptmin = *(binningdet.begin()), ptmax = *(binningdet.rbegin());
    TH1 *htrue = new TH1D("htrue", "true spectrum", binningpart.size()-1, binningpart.data()),
        *hsmeared = new TH1D("hsmeared", "det mc", binningdet.size()-1, binningdet.data()), 
        *hsmearedClosure = new TH1D("hsmearedClosure", "det mc (for closure test)", binningdet.size() - 1, binningdet.data()),
        *htrueClosure = new TH1D("htrueClosure", "true spectrum (for closure test)", binningpart.size() - 1, binningpart.data()),
        *htrueFull = new TH1D("htrueFull", "non-truncated true spectrum", binningpart.size() - 1, binningpart.data()),
        *htrueFullClosure = new TH1D("htrueFullClosure", "non-truncated true spectrum (for closure test)", binningpart.size() - 1, binningpart.data()),
        *hpriorsClosure = new TH1D("hpriorsClosure", "non-truncated true spectrum (for closure test, same jets as repsonse matrix)", binningpart.size() - 1, binningpart.data());
    TH2 *responseMatrix = new TH2D("responseMatrix", "response matrix", binningdet.size()-1, binningdet.data(), binningpart.size()-1, binningpart.data()),
        *responseMatrixClosure = new TH2D("responseMatrixClosure", "response matrix (for closure test)", binningdet.size()-1, binningdet.data(), binningpart.size()-1, binningpart.data());
  
    {
        TRandom closuresplit;
        std::stringstream filemc;
        filemc << datadir << "/mc/merged_calo/JetSubstructureTree_FullJets_R" << std::setw(2) << std::setfill('0') << int(radius*10.) << "_INT7" << "_pt" << int(ptcut) << "_merged.root";
        std::unique_ptr<TFile> fread(TFile::Open(filemc.str().data(), "READ"));
        TTreeReader mcreader(GetDataTree(*fread));
        TTreeReaderValue<double>  ptrec(mcreader, "PtJetRec"), 
                                  ptsim(mcreader, "PtJetSim"), 
                                  nefrec(mcreader, "NEFRec"),
                                  weight(mcreader, "PythiaWeight");
        TTreeReaderValue<int>     pthardbin(mcreader, "PtHardBin");
        bool closureUseSpectrum;
        for(auto en : mcreader){
            if(IsOutlierFast(*ptsim, *pthardbin)) continue;
            double rdm = closuresplit.Uniform();
            closureUseSpectrum = (rdm < 0.2);
            htrueFull->Fill(*ptsim, *weight);
            if(closureUseSpectrum) htrueFullClosure->Fill(*ptsim, *weight);
            else hpriorsClosure->Fill(*ptsim, *weight);
            if(*ptrec > ptmin && *ptrec < ptmax){
                htrue->Fill(*ptsim, *weight);
                hsmeared->Fill(*ptrec, *weight);
                responseMatrix->Fill(*ptrec, *ptsim, *weight);

                if(closureUseSpectrum) {
                    hsmearedClosure->Fill(*ptrec, *weight);
                    htrueClosure->Fill(*ptsim, *weight);
                } else {
                    responseMatrixClosure->Fill(*ptrec, *ptsim, *weight);
                }
            }
        }
    }

    // Calculate kinematic efficiency
    std::cout << "[SVD unfolding] Make kinematic efficiecny for raw unfolding ..." << std::endl;
    auto effKine = histcopy(htrue);
    effKine->SetDirectory(nullptr);
    effKine->SetName("effKine");
    effKine->Divide(effKine, htrueFull, 1., 1., "b");

    std::cout << "[SVD unfolding] ... and for closure test" << std::endl;
    auto effKineClosure = histcopy(htrueClosure);
    effKineClosure->SetDirectory(nullptr);
    effKineClosure->SetName("effKineClosure");
    effKineClosure->Divide(htrueFullClosure);

    std::cout << "[SVD unfolding] Building RooUnfold response" << std::endl;
    RooUnfoldResponse response(nullptr, htrueFull, responseMatrix), responseClosure(nullptr, hpriorsClosure, responseMatrixClosure);

    std::cout << "Running unfolding" << std::endl;
    std::map<std::string, std::vector<TObject *>> iterresults;
    RooUnfold::ErrorTreatment errorTreatment = RooUnfold::kCovToy;//ariance;
    const double kSizeEmcalPhi = 1.88,
                 kSizeEmcalEta = 1.4;
    double acceptance = (kSizeEmcalPhi - 2 * radius) * (kSizeEmcalEta - 2 * radius) / (TMath::TwoPi());
    double crosssection = 57.8;
    double epsilon_vtx = 0.8228; // for the moment hard coded, for future analyses determined automatically from the output
    for(auto reg : ROOT::TSeqI(1, hraw->GetXaxis()->GetNbins())){
        std::cout << "[SVD unfolding] Regularization " << reg << "\n================================================================\n";
        std::cout << "[SVD unfolding] Running unfolding" << std::endl;
        RooUnfoldSvd unfolder(&response, hraw, reg);
        auto unfolded = unfolder.Hreco(errorTreatment);
        unfolded->SetName(Form("unfoldedReg%d", reg));
        unfolded->SetDirectory(nullptr);
        TH1 *dvec(nullptr);
        if(auto imp = unfolder.Impl()){
            dvec = imp->GetD();
            dvec->SetName(Form("dvectorReg%d", reg));
            dvec->SetDirectory(nullptr);
        }
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "[SVD unfolding] Running MC closure test" << std::endl;
        RooUnfoldSvd unfolderClosure(&responseClosure, hsmearedClosure, reg, 1000, "unfolderClosure", "unfolderClosure");
        auto unfoldedClosure = unfolderClosure.Hreco(errorTreatment);
        unfoldedClosure->SetName(Form("unfoldedClosureReg%d", reg));
        unfoldedClosure->SetDirectory(nullptr);
        TH1 * dvecClosure(nullptr);
        if(auto imp = unfolderClosure.Impl()){
            dvecClosure = imp->GetD();
            dvecClosure->SetName(Form("dvectorClosureReg%d", reg));
            dvecClosure->SetDirectory(nullptr);
        }

        // back-folding test
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "[SVD unfolding] Running back-folding test" << std::endl;
        auto backfolded = MakeRefolded1D(hraw, unfolded, response);
        backfolded->SetName(Form("backfolded_reg%d", reg));
        backfolded->SetDirectory(nullptr);
        auto backfoldedClosure = MakeRefolded1D(hsmearedClosure, unfoldedClosure, responseClosure);
        backfoldedClosure->SetName(Form("backfoldedClosure_reg%d", reg));
        backfoldedClosure->SetDirectory(nullptr);

        // normalize spectrum (but write as new object)
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "[SVD unfolding] Normalizing spectrum" << std::endl;
        auto normalized = histcopy(unfolded);
        normalized->SetDirectory(nullptr);
        normalized->SetNameTitle(Form("normalizedReg%d", reg), Form("Normalized for regularization %d", reg));
        normalized->Scale(crosssection*epsilon_vtx/acceptance);
        normalizeBinWidth(normalized);

        // preparing for output finding
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "[SVD unfolding] Building output list" << std::endl;
        std::vector<TObject *>resultvec = {unfolded, normalized, backfolded, unfoldedClosure, backfoldedClosure};
        if(dvec) resultvec.push_back(histcopy(dvec));
        if(dvecClosure) resultvec.push_back(histcopy(dvecClosure));
        iterresults[Form("regularization%d", reg)] = resultvec;
        std::cout << "----------------------------------------------------------------------\n";
        std::cout << "[SVD unfolding] regularization done" << std::endl;
        std::cout << "======================================================================\n";
    }

    // write everything
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "[SVD unfolding] Writeing output" << std::endl;
    std::unique_ptr<TFile> writer(TFile::Open(Form("corrected1DSVD_R%02d_pt%d.root", int(radius*10.), int(ptcut)), "RECREATE"));
    writer->mkdir("rawlevel");
    writer->cd("rawlevel");
    hraw->Write();
    lumihist->Write();
    for(auto m : mcspectra) {normalizeBinWidth(m.second); m.second->Write();}
    for(auto d : dataspectra) {normalizeBinWidth(d.second); d.second->Write();}
    for(auto e : efficiencies) e.second->Write();
    for(auto n : hnorm) n.second->Write();
    for(auto r : ratios) r.second->Write();
    for(auto c : centnotrdCorrection) c->Write();
    writer->mkdir("detectorresponse");
    writer->cd("detectorresponse");
    htrueFull->Write();
    htrueFullClosure->Write();
    htrue->Write();
    htrueClosure->Write();
    hpriorsClosure->Write();
    hsmeared->Write();
    hsmearedClosure->Write();
    responseMatrix->Write();
    responseMatrixClosure->Write();
    hraw->Write();
    effKine->Write();
    effKineClosure->Write();
    for(const auto &k : getSortedKeys(iterresults)) {
        writer->mkdir(k.data());
        writer->cd(k.data());
        for(auto h : iterresults.find(k)->second) h->Write();
    }
    std::cout << "----------------------------------------------------------------------\n";
    std::cout << "[SVD unfolding] All done" << std::endl;
    std::cout << "======================================================================\n";
}