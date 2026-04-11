// modelfile_creator.cpp
// Creates custom Ollama models via the POST /api/create JSON endpoint.
// No Modelfile text needed — uses structured JSON fields directly.

#include "modelfile_creator.h"
#include "theme.h"

#include <Poco/URI.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NetException.h>
#include <Poco/StreamCopier.h>
#include <Poco/Timespan.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <Poco/Exception.h>

#include <sstream>

// ── Event definitions ─────────────────────────────────────────────
wxDEFINE_EVENT(wxEVT_MC_CREATE_PROGRESS, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MC_CREATE_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MC_CREATE_ERROR,    wxCommandEvent);

enum {
    ID_MC_CREATE  = wxID_HIGHEST + 300,
    ID_MC_PREVIEW = wxID_HIGHEST + 301,
};

wxBEGIN_EVENT_TABLE(ModelfileCreatorDialog, wxDialog)
    EVT_BUTTON(ID_MC_CREATE,  ModelfileCreatorDialog::OnCreate)
    EVT_BUTTON(ID_MC_PREVIEW, ModelfileCreatorDialog::OnPreview)
    EVT_BUTTON(wxID_CANCEL,   ModelfileCreatorDialog::OnCancel)
    EVT_COMMAND(wxID_ANY, wxEVT_MC_CREATE_PROGRESS, ModelfileCreatorDialog::OnCreateProgress)
    EVT_COMMAND(wxID_ANY, wxEVT_MC_CREATE_COMPLETE, ModelfileCreatorDialog::OnCreateComplete)
    EVT_COMMAND(wxID_ANY, wxEVT_MC_CREATE_ERROR,    ModelfileCreatorDialog::OnCreateError)
wxEND_EVENT_TABLE()

// ═══════════════════════════════════════════════════════════════════
//  Dialog
// ═══════════════════════════════════════════════════════════════════

ModelfileCreatorDialog::ModelfileCreatorDialog(wxWindow* parent,
                                               const std::string& apiUrl,
                                               const std::vector<std::string>& installedModels,
                                               const ThemeData* theme)
    : wxDialog(parent, wxID_ANY, "Create Custom Model", wxDefaultPosition, wxSize(620, 580),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_apiUrl(apiUrl)
    , m_installedModels(installedModels)
    , m_theme(theme)
    , m_created(false)
    , m_busy(false)
{
    wxFont f = GetFont();
    f.SetPointSize(11);
    SetFont(f);

    CreateControls();
    ApplyTheme();
}

ModelfileCreatorDialog::~ModelfileCreatorDialog()
{
    if (m_cancelFlag) m_cancelFlag->store(true);
}

void ModelfileCreatorDialog::CreateControls()
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // ── Model name ────────────────────────────────────────────────
    mainSizer->Add(new wxStaticText(this, wxID_ANY, "Model Name (required):"),
                   0, wxLEFT | wxRIGHT | wxTOP, 8);
    m_modelNameCtrl = new wxTextCtrl(this, wxID_ANY);
    m_modelNameCtrl->SetHint("e.g. my-assistant, code-helper");
    mainSizer->Add(m_modelNameCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ── Base model (FROM) ─────────────────────────────────────────
    mainSizer->Add(new wxStaticText(this, wxID_ANY, "Base Model (FROM):"),
                   0, wxLEFT | wxRIGHT, 8);
    wxArrayString choices;
    for (const auto& m : m_installedModels)
        choices.Add(wxString::FromUTF8(m));
    m_baseModelCombo = new wxComboBox(this, wxID_ANY, "",
                                      wxDefaultPosition, wxDefaultSize, choices,
                                      wxCB_DROPDOWN);
    if (!choices.empty()) m_baseModelCombo->SetSelection(0);
    mainSizer->Add(m_baseModelCombo, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ── System prompt ─────────────────────────────────────────────
    mainSizer->Add(new wxStaticText(this, wxID_ANY, "System Prompt:"),
                   0, wxLEFT | wxRIGHT, 8);
    m_systemPromptCtrl = new wxTextCtrl(this, wxID_ANY, "",
                                         wxDefaultPosition, wxSize(-1, 100),
                                         wxTE_MULTILINE);
    m_systemPromptCtrl->SetHint("e.g. You are a helpful coding assistant that responds concisely.");
    mainSizer->Add(m_systemPromptCtrl, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ── Parameters grid ───────────────────────────────────────────
    mainSizer->Add(new wxStaticText(this, wxID_ANY, "Parameters (leave blank for defaults):"),
                   0, wxLEFT | wxRIGHT, 8);

    auto* grid = new wxFlexGridSizer(4, 4, 4);  // 4 cols, 4px gaps
    grid->AddGrowableCol(1);
    grid->AddGrowableCol(3);

    auto addParam = [&](const wxString& label, wxTextCtrl*& ctrl,
                        const wxString& hint, const wxString& def = "") {
        grid->Add(new wxStaticText(this, wxID_ANY, label),
                  0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT);
        ctrl = new wxTextCtrl(this, wxID_ANY, def, wxDefaultPosition, wxSize(80, -1));
        ctrl->SetHint(hint);
        grid->Add(ctrl, 1, wxEXPAND);
    };

    addParam("Temperature:", m_temperatureCtrl, "0.8");
    addParam("Context (num_ctx):", m_numCtxCtrl, "2048");
    addParam("Top K:", m_topKCtrl, "40");
    addParam("Top P:", m_topPCtrl, "0.9");
    addParam("Repeat Penalty:", m_repeatPenaltyCtrl, "1.1");
    addParam("Seed:", m_seedCtrl, "0");

    mainSizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ── Stop sequences ────────────────────────────────────────────
    mainSizer->Add(new wxStaticText(this, wxID_ANY, "Stop Sequences (comma-separated):"),
                   0, wxLEFT | wxRIGHT, 8);
    m_stopCtrl = new wxTextCtrl(this, wxID_ANY);
    m_stopCtrl->SetHint("e.g. <|end|>, [DONE]");
    mainSizer->Add(m_stopCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ── Progress / status ─────────────────────────────────────────
    m_progressBar = new wxGauge(this, wxID_ANY, 100);
    m_progressBar->Hide();
    mainSizer->Add(m_progressBar, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);

    m_statusText = new wxStaticText(this, wxID_ANY, "");
    mainSizer->Add(m_statusText, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // ── Buttons ───────────────────────────────────────────────────
    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_previewButton = new wxButton(this, ID_MC_PREVIEW, "Preview Modelfile");
    btnSizer->Add(m_previewButton, 0, wxRIGHT, 8);
    btnSizer->AddStretchSpacer();
    m_createButton = new wxButton(this, ID_MC_CREATE, "Create Model");
    btnSizer->Add(m_createButton, 0, wxRIGHT, 8);
    btnSizer->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);
    mainSizer->Add(btnSizer, 0, wxEXPAND | wxALL, 8);

    SetSizer(mainSizer);
}

void ModelfileCreatorDialog::ApplyTheme()
{
    if (!m_theme) return;

    SetBackgroundColour(m_theme->bgSidebar);

    for (auto* child : GetChildren()) {
        if (auto* lbl = dynamic_cast<wxStaticText*>(child))
            lbl->SetForegroundColour(m_theme->textPrimary);
        else if (auto* btn = dynamic_cast<wxButton*>(child))
        {
            btn->SetBackgroundColour(m_theme->bgInputField);
            btn->SetForegroundColour(m_theme->textPrimary);
        }
        else if (auto* cb = dynamic_cast<wxComboBox*>(child))
        {
            cb->SetBackgroundColour(m_theme->bgInputField);
            cb->SetForegroundColour(m_theme->textPrimary);
        }
        else if (auto* tc = dynamic_cast<wxTextCtrl*>(child))
        {
            tc->SetBackgroundColour(m_theme->bgInputField);
            tc->SetForegroundColour(m_theme->textPrimary);
        }
    }

    // The Create button gets accent styling
    m_createButton->SetBackgroundColour(m_theme->accentButton);
    m_createButton->SetForegroundColour(m_theme->accentButtonText);
}

// ── Build the JSON request body for /api/create ───────────────────

std::string ModelfileCreatorDialog::BuildRequestJson() const
{
    Poco::JSON::Object root;

    root.set("model", m_modelNameCtrl->GetValue().ToStdString());
    root.set("from", m_baseModelCombo->GetValue().ToStdString());

    // System prompt
    std::string sys = m_systemPromptCtrl->GetValue().ToStdString();
    if (!sys.empty())
        root.set("system", sys);

    // Parameters object
    Poco::JSON::Object params;
    bool hasParams = false;

    auto addFloat = [&](wxTextCtrl* ctrl, const std::string& key) {
        std::string val = ctrl->GetValue().ToStdString();
        if (!val.empty()) {
            try { params.set(key, std::stod(val)); hasParams = true; }
            catch (...) {}
        }
    };
    auto addInt = [&](wxTextCtrl* ctrl, const std::string& key) {
        std::string val = ctrl->GetValue().ToStdString();
        if (!val.empty()) {
            try { params.set(key, std::stoi(val)); hasParams = true; }
            catch (...) {}
        }
    };

    addFloat(m_temperatureCtrl, "temperature");
    addInt(m_numCtxCtrl, "num_ctx");
    addInt(m_topKCtrl, "top_k");
    addFloat(m_topPCtrl, "top_p");
    addFloat(m_repeatPenaltyCtrl, "repeat_penalty");
    addInt(m_seedCtrl, "seed");

    // Stop sequences — split by comma
    std::string stopStr = m_stopCtrl->GetValue().ToStdString();
    if (!stopStr.empty()) {
        Poco::JSON::Array stopArr;
        std::istringstream ss(stopStr);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            // Trim whitespace
            size_t s = tok.find_first_not_of(" \t");
            size_t e = tok.find_last_not_of(" \t");
            if (s != std::string::npos)
                stopArr.add(tok.substr(s, e - s + 1));
        }
        if (stopArr.size() > 0) {
            params.set("stop", stopArr);
            hasParams = true;
        }
    }

    if (hasParams)
        root.set("parameters", params);

    root.set("stream", true);

    std::ostringstream oss;
    root.stringify(oss);
    return oss.str();
}

// ── Preview Modelfile ─────────────────────────────────────────────

void ModelfileCreatorDialog::OnPreview(wxCommandEvent&)
{
    // Build a traditional Modelfile text for display
    std::string preview;
    preview += "FROM " + m_baseModelCombo->GetValue().ToStdString() + "\n\n";

    std::string sys = m_systemPromptCtrl->GetValue().ToStdString();
    if (!sys.empty())
        preview += "SYSTEM \"\"\"" + sys + "\"\"\"\n\n";

    auto addParam = [&](wxTextCtrl* ctrl, const std::string& name) {
        std::string val = ctrl->GetValue().ToStdString();
        if (!val.empty())
            preview += "PARAMETER " + name + " " + val + "\n";
    };

    addParam(m_temperatureCtrl, "temperature");
    addParam(m_numCtxCtrl, "num_ctx");
    addParam(m_topKCtrl, "top_k");
    addParam(m_topPCtrl, "top_p");
    addParam(m_repeatPenaltyCtrl, "repeat_penalty");
    addParam(m_seedCtrl, "seed");

    std::string stopStr = m_stopCtrl->GetValue().ToStdString();
    if (!stopStr.empty()) {
        std::istringstream ss(stopStr);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            size_t s = tok.find_first_not_of(" \t");
            size_t e = tok.find_last_not_of(" \t");
            if (s != std::string::npos)
                preview += "PARAMETER stop \"" + tok.substr(s, e - s + 1) + "\"\n";
        }
    }

    wxMessageBox(wxString::FromUTF8(preview), "Modelfile Preview",
                 wxOK | wxICON_INFORMATION, this);
}

// ── Create ────────────────────────────────────────────────────────

void ModelfileCreatorDialog::OnCreate(wxCommandEvent&)
{
    std::string name = m_modelNameCtrl->GetValue().ToStdString();
    if (name.empty()) {
        m_statusText->SetLabel("Model name is required");
        return;
    }
    std::string base = m_baseModelCombo->GetValue().ToStdString();
    if (base.empty()) {
        m_statusText->SetLabel("Base model is required");
        return;
    }

    std::string json = BuildRequestJson();

    m_busy = true;
    m_createButton->Enable(false);
    m_previewButton->Enable(false);
    m_progressBar->Show();
    m_progressBar->Pulse();
    m_statusText->SetLabel("Creating model \"" + name + "\"...");
    Layout();

    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    auto* t = new MCCreateThread(this, m_apiUrl, json, m_cancelFlag);
    if (t->Run() != wxTHREAD_NO_ERROR) {
        delete t;
        m_cancelFlag.reset();
        m_busy = false;
        m_createButton->Enable(true);
        m_previewButton->Enable(true);
        m_progressBar->Hide();
        m_statusText->SetLabel("Failed to start create thread");
        Layout();
    }
}

void ModelfileCreatorDialog::OnCreateProgress(wxCommandEvent& ev)
{
    m_statusText->SetLabel(ev.GetString());
    m_progressBar->Pulse();
}

void ModelfileCreatorDialog::OnCreateComplete(wxCommandEvent& ev)
{
    m_busy = false;
    m_created = true;
    m_createButton->Enable(true);
    m_previewButton->Enable(true);
    m_progressBar->Hide();
    m_statusText->SetLabel("Model created successfully!");
    Layout();
}

void ModelfileCreatorDialog::OnCreateError(wxCommandEvent& ev)
{
    m_busy = false;
    m_createButton->Enable(true);
    m_previewButton->Enable(true);
    m_progressBar->Hide();
    m_statusText->SetLabel("Error: " + ev.GetString());
    Layout();
}

void ModelfileCreatorDialog::OnCancel(wxCommandEvent&)
{
    if (m_cancelFlag) m_cancelFlag->store(true);
    EndModal(wxID_CANCEL);
}

// ═══════════════════════════════════════════════════════════════════
//  MCCreateThread — streams POST /api/create
// ═══════════════════════════════════════════════════════════════════

MCCreateThread::MCCreateThread(wxEvtHandler* handler, const std::string& apiUrl,
                               const std::string& requestJson,
                               std::shared_ptr<std::atomic<bool>> cancelFlag)
    : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_apiUrl(apiUrl),
      m_requestJson(requestJson), m_cancelFlag(cancelFlag)
{}

bool MCCreateThread::SafePost(wxCommandEvent* ev)
{
    if (m_cancelFlag->load()) { delete ev; return false; }
    wxQueueEvent(m_handler, ev);
    return true;
}

wxThread::ExitCode MCCreateThread::Entry()
{
    auto cancelled = [this]() { return m_cancelFlag->load(); };
    try {
        Poco::URI uri(m_apiUrl + "/api/create");
        Poco::Net::HTTPClientSession sess(uri.getHost(), uri.getPort());
        sess.setTimeout(Poco::Timespan(600, 0));

        Poco::Net::HTTPRequest req(Poco::Net::HTTPRequest::HTTP_POST,
                                   uri.getPathAndQuery(),
                                   Poco::Net::HTTPMessage::HTTP_1_1);
        req.setContentType("application/json");
        req.setContentLength(m_requestJson.size());

        sess.sendRequest(req) << m_requestJson;

        Poco::Net::HTTPResponse resp;
        std::istream& in = sess.receiveResponse(resp);

        if (resp.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
            std::string errBody;
            Poco::StreamCopier::copyToString(in, errBody);
            if (!cancelled()) {
                auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_ERROR);
                ev->SetString(wxString::Format("HTTP %d: %s",
                              (int)resp.getStatus(), wxString::FromUTF8(errBody)));
                SafePost(ev);
            }
            return (ExitCode)0;
        }

        // Stream newline-delimited JSON: {"status":"..."}
        std::string line;
        while (std::getline(in, line)) {
            if (cancelled()) return (ExitCode)0;
            if (line.empty()) continue;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            try {
                Poco::JSON::Parser p;
                auto obj = p.parse(line).extract<Poco::JSON::Object::Ptr>();
                std::string status = obj->optValue<std::string>("status", "");

                if (status == "success") {
                    if (!cancelled()) {
                        auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_COMPLETE);
                        ev->SetString("success");
                        SafePost(ev);
                    }
                    return (ExitCode)0;
                }

                // Check for error field
                std::string error = obj->optValue<std::string>("error", "");
                if (!error.empty()) {
                    if (!cancelled()) {
                        auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_ERROR);
                        ev->SetString(wxString::FromUTF8(error));
                        SafePost(ev);
                    }
                    return (ExitCode)0;
                }

                if (!status.empty() && !cancelled()) {
                    auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_PROGRESS);
                    ev->SetString(wxString::FromUTF8(status));
                    SafePost(ev);
                }
            }
            catch (...) {}
        }

        // Stream ended — assume success
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_COMPLETE);
            ev->SetString("complete");
            SafePost(ev);
        }
    }
    catch (const Poco::Exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_ERROR);
            ev->SetString(wxString::FromUTF8(ex.displayText()));
            SafePost(ev);
        }
    }
    catch (const std::exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MC_CREATE_ERROR);
            ev->SetString(wxString::FromUTF8(ex.what()));
            SafePost(ev);
        }
    }
    return (ExitCode)0;
}
