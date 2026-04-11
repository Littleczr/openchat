// model_manager.cpp
#include "model_manager.h"
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
#include <iomanip>

// ── Event definitions ─────────────────────────────────────────────
wxDEFINE_EVENT(wxEVT_MM_MODELS_LOADED,   wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MM_LOAD_ERROR,      wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MM_DELETE_OK,        wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MM_DELETE_ERROR,     wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MM_PULL_PROGRESS,    wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MM_PULL_COMPLETE,    wxCommandEvent);
wxDEFINE_EVENT(wxEVT_MM_PULL_ERROR,       wxCommandEvent);

// ── Helper: human-readable size ───────────────────────────────────
static std::string FormatSize(int64_t bytes)
{
    if (bytes < 0) return "?";
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int idx = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && idx < 4) { size /= 1024.0; idx++; }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(idx == 0 ? 0 : 1) << size << " " << units[idx];
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════
//  ModelManagerDialog
// ═══════════════════════════════════════════════════════════════════

enum {
    ID_MM_DELETE  = wxID_HIGHEST + 200,
    ID_MM_PULL    = wxID_HIGHEST + 201,
    ID_MM_REFRESH = wxID_HIGHEST + 202,
    ID_MM_CREATE  = wxID_HIGHEST + 203,
};

wxBEGIN_EVENT_TABLE(ModelManagerDialog, wxDialog)
    EVT_BUTTON(ID_MM_DELETE,  ModelManagerDialog::OnDeleteClicked)
    EVT_BUTTON(ID_MM_PULL,    ModelManagerDialog::OnPullClicked)
    EVT_BUTTON(ID_MM_REFRESH, ModelManagerDialog::OnRefreshClicked)
    EVT_BUTTON(ID_MM_CREATE,  ModelManagerDialog::OnCreateClicked)
    EVT_BUTTON(wxID_CLOSE,    ModelManagerDialog::OnClose)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_MODELS_LOADED,  ModelManagerDialog::OnModelsLoaded)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_LOAD_ERROR,      ModelManagerDialog::OnLoadError)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_DELETE_OK,        ModelManagerDialog::OnDeleteOK)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_DELETE_ERROR,     ModelManagerDialog::OnDeleteError)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_PULL_PROGRESS,    ModelManagerDialog::OnPullProgress)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_PULL_COMPLETE,    ModelManagerDialog::OnPullComplete)
    EVT_COMMAND(wxID_ANY, wxEVT_MM_PULL_ERROR,       ModelManagerDialog::OnPullError)
wxEND_EVENT_TABLE()

ModelManagerDialog::ModelManagerDialog(wxWindow* parent, const std::string& apiUrl,
                                       const ThemeData* theme)
    : wxDialog(parent, wxID_ANY, "Manage Models", wxDefaultPosition, wxSize(560, 440),
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_apiUrl(apiUrl)
    , m_busy(false)
    , m_theme(theme)
{
    CreateControls();
    CallAfter([this]() { StartListModels(); });
}

ModelManagerDialog::~ModelManagerDialog()
{
    if (m_cancelFlag) m_cancelFlag->store(true);
}

void ModelManagerDialog::CreateControls()
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // ── Installed models list ─────────────────────────────────────
    auto* listLabel = new wxStaticText(this, wxID_ANY, "Installed Models:");
    mainSizer->Add(listLabel, 0, wxALL, 5);

    m_modelList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 200),
                                 wxLC_REPORT | wxLC_SINGLE_SEL);
    m_modelList->AppendColumn("Model", wxLIST_FORMAT_LEFT, 280);
    m_modelList->AppendColumn("Size", wxLIST_FORMAT_RIGHT, 90);
    m_modelList->AppendColumn("Quantization", wxLIST_FORMAT_LEFT, 110);
    mainSizer->Add(m_modelList, 1, wxEXPAND | wxLEFT | wxRIGHT, 5);

    // ── Delete / Refresh / Create row ───────────────────────────────
    auto* actionSizer = new wxBoxSizer(wxHORIZONTAL);
    m_deleteButton = new wxButton(this, ID_MM_DELETE, "Delete Selected");
    m_refreshButton = new wxButton(this, ID_MM_REFRESH, "Refresh");
    auto* createBtn = new wxButton(this, ID_MM_CREATE, "Create Model...");
    actionSizer->Add(m_deleteButton, 0, wxRIGHT, 5);
    actionSizer->Add(m_refreshButton, 0, wxRIGHT, 5);
    actionSizer->AddStretchSpacer();
    actionSizer->Add(createBtn, 0);
    mainSizer->Add(actionSizer, 0, wxEXPAND | wxALL, 5);

    // ── Pull section ──────────────────────────────────────────────
    mainSizer->AddSpacer(5);
    auto* pullLabel = new wxStaticText(this, wxID_ANY, "Pull New Model:");
    mainSizer->Add(pullLabel, 0, wxLEFT | wxRIGHT, 5);

    auto* pullSizer = new wxBoxSizer(wxHORIZONTAL);
    m_pullInput = new wxTextCtrl(this, wxID_ANY, "",
                                 wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_pullInput->SetHint("e.g. llama3:8b or gemma2:2b");
    m_pullButton = new wxButton(this, ID_MM_PULL, "Pull");
    pullSizer->Add(m_pullInput, 1, wxEXPAND | wxRIGHT, 5);
    pullSizer->Add(m_pullButton, 0);
    mainSizer->Add(pullSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);

    // ── Progress bar + status ─────────────────────────────────────
    m_progressBar = new wxGauge(this, wxID_ANY, 100);
    m_progressBar->Hide();
    mainSizer->Add(m_progressBar, 0, wxEXPAND | wxLEFT | wxRIGHT, 5);

    m_statusText = new wxStaticText(this, wxID_ANY, "Loading...");
    mainSizer->Add(m_statusText, 0, wxALL, 5);

    // ── Close button ──────────────────────────────────────────────
    auto* closeSizer = new wxBoxSizer(wxHORIZONTAL);
    closeSizer->AddStretchSpacer();
    closeSizer->Add(new wxButton(this, wxID_CLOSE, "Close"), 0);
    mainSizer->Add(closeSizer, 0, wxEXPAND | wxALL, 5);

    SetSizer(mainSizer);

    // ── Apply theme colors ───────────────────────────────────────
    if (m_theme) {
        SetBackgroundColour(m_theme->bgToolbar);

        for (auto* child : GetChildren()) {
            if (auto* lbl = dynamic_cast<wxStaticText*>(child)) {
                lbl->SetForegroundColour(m_theme->textPrimary);
            } else if (auto* btn = dynamic_cast<wxButton*>(child)) {
                btn->SetBackgroundColour(m_theme->modelPillBg);
                btn->SetForegroundColour(m_theme->textPrimary);
            } else if (auto* tc = dynamic_cast<wxTextCtrl*>(child)) {
                tc->SetBackgroundColour(m_theme->bgInputField);
                tc->SetForegroundColour(m_theme->textPrimary);
            } else if (auto* gauge = dynamic_cast<wxGauge*>(child)) {
                gauge->SetBackgroundColour(m_theme->bgToolbar);
            }
        }

        // Style the list control specifically
        m_modelList->SetBackgroundColour(m_theme->sidebarSelected);
        m_modelList->SetForegroundColour(m_theme->textPrimary);
    }
}

void ModelManagerDialog::SetBusy(bool busy)
{
    m_busy = busy;
    m_deleteButton->Enable(!busy);
    m_pullButton->Enable(!busy);
    m_refreshButton->Enable(!busy);
    m_pullInput->Enable(!busy);
    m_progressBar->Show(busy);
    if (busy) m_progressBar->Pulse();
    else      m_progressBar->SetValue(0);
    Layout();
}

void ModelManagerDialog::StartListModels()
{
    SetBusy(true);
    m_statusText->SetLabel("Fetching installed models...");
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);

    auto* t = new MMListThread(this, m_apiUrl, m_cancelFlag);
    if (t->Run() != wxTHREAD_NO_ERROR) {
        delete t;
        m_cancelFlag.reset();
        SetBusy(false);
        m_statusText->SetLabel("Failed to start model fetch");
    }
}

// ── List events ───────────────────────────────────────────────────

void ModelManagerDialog::OnModelsLoaded(wxCommandEvent& ev)
{
    SetBusy(false);
    m_modelList->DeleteAllItems();

    // Packed format: "name\tsize\tquant\n" per model
    std::istringstream stream(ev.GetString().ToStdString());
    std::string line;
    long row = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // Split by tab
        std::istringstream ls(line);
        std::string name, size, quant;
        std::getline(ls, name, '\t');
        std::getline(ls, size, '\t');
        std::getline(ls, quant, '\t');

        long idx = m_modelList->InsertItem(row, wxString::FromUTF8(name));
        m_modelList->SetItem(idx, 1, wxString::FromUTF8(size));
        m_modelList->SetItem(idx, 2, wxString::FromUTF8(quant));
        row++;
    }
    m_statusText->SetLabel(wxString::Format("%ld model(s) installed", row));
}

void ModelManagerDialog::OnLoadError(wxCommandEvent& ev)
{
    SetBusy(false);
    m_statusText->SetLabel("Error: " + ev.GetString());
}

// ── Refresh ───────────────────────────────────────────────────────

void ModelManagerDialog::OnRefreshClicked(wxCommandEvent&)
{
    StartListModels();
}

// ── Delete ────────────────────────────────────────────────────────

void ModelManagerDialog::OnDeleteClicked(wxCommandEvent&)
{
    long sel = m_modelList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel < 0) {
        m_statusText->SetLabel("Select a model to delete");
        return;
    }
    std::string modelName = m_modelList->GetItemText(sel).ToStdString();

    if (wxMessageBox(wxString::Format("Delete model \"%s\"?\nThis cannot be undone.",
                                       modelName),
                     "Confirm Delete", wxYES_NO | wxICON_WARNING, this) != wxYES)
        return;

    SetBusy(true);
    m_statusText->SetLabel("Deleting " + modelName + "...");
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);

    auto* t = new MMDeleteThread(this, m_apiUrl, modelName, m_cancelFlag);
    if (t->Run() != wxTHREAD_NO_ERROR) {
        delete t;
        m_cancelFlag.reset();
        SetBusy(false);
        m_statusText->SetLabel("Failed to start delete");
    }
}

void ModelManagerDialog::OnDeleteOK(wxCommandEvent& ev)
{
    m_statusText->SetLabel("Deleted: " + ev.GetString());
    StartListModels(); // Refresh
}

void ModelManagerDialog::OnDeleteError(wxCommandEvent& ev)
{
    SetBusy(false);
    m_statusText->SetLabel("Delete failed: " + ev.GetString());
}

// ── Pull ──────────────────────────────────────────────────────────

void ModelManagerDialog::OnPullClicked(wxCommandEvent&)
{
    std::string modelName = m_pullInput->GetValue().ToStdString();
    // Trim whitespace
    modelName.erase(0, modelName.find_first_not_of(" \t\r\n"));
    modelName.erase(modelName.find_last_not_of(" \t\r\n") + 1);

    if (modelName.empty()) {
        m_statusText->SetLabel("Enter a model name to pull (e.g. llama3:8b)");
        return;
    }

    SetBusy(true);
    m_progressBar->SetRange(100);
    m_progressBar->SetValue(0);
    m_progressBar->Show();
    m_statusText->SetLabel("Pulling " + modelName + "...");
    Layout();

    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    auto* t = new MMPullThread(this, m_apiUrl, modelName, m_cancelFlag);
    if (t->Run() != wxTHREAD_NO_ERROR) {
        delete t;
        m_cancelFlag.reset();
        SetBusy(false);
        m_statusText->SetLabel("Failed to start pull");
    }
}

void ModelManagerDialog::OnPullProgress(wxCommandEvent& ev)
{
    // Int = percentage (0-100), String = status text
    int pct = ev.GetInt();
    if (pct >= 0 && pct <= 100) {
        m_progressBar->SetValue(pct);
    } else {
        m_progressBar->Pulse();
    }
    m_statusText->SetLabel(ev.GetString());
}

void ModelManagerDialog::OnPullComplete(wxCommandEvent& ev)
{
    m_statusText->SetLabel("Pull complete: " + ev.GetString());
    m_pullInput->Clear();
    StartListModels(); // Refresh
}

void ModelManagerDialog::OnPullError(wxCommandEvent& ev)
{
    SetBusy(false);
    m_statusText->SetLabel("Pull failed: " + ev.GetString());
}

// ── Close ─────────────────────────────────────────────────────────

void ModelManagerDialog::OnClose(wxCommandEvent&)
{
    if (m_cancelFlag) m_cancelFlag->store(true);
    EndModal(wxID_CLOSE);
}

// ── Create Model ──────────────────────────────────────────────────

void ModelManagerDialog::OnCreateClicked(wxCommandEvent&)
{
    // Gather installed model names from the list control
    std::vector<std::string> models;
    for (long i = 0; i < m_modelList->GetItemCount(); i++)
        models.push_back(m_modelList->GetItemText(i).ToStdString());

    ModelfileCreatorDialog dlg(this, m_apiUrl, models, m_theme);
    dlg.ShowModal();

    // Refresh if a model was created
    if (dlg.WasModelCreated())
        StartListModels();
}

// ═══════════════════════════════════════════════════════════════════
//  MMListThread
// ═══════════════════════════════════════════════════════════════════

MMListThread::MMListThread(wxEvtHandler* handler, const std::string& apiUrl,
                           std::shared_ptr<std::atomic<bool>> cancelFlag)
    : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_apiUrl(apiUrl), m_cancelFlag(cancelFlag)
{}

bool MMListThread::SafePost(wxCommandEvent* ev)
{
    if (m_cancelFlag->load()) { delete ev; return false; }
    wxQueueEvent(m_handler, ev);
    return true;
}

wxThread::ExitCode MMListThread::Entry()
{
    auto cancelled = [this]() { return m_cancelFlag->load(); };
    try {
        Poco::URI uri(m_apiUrl + "/api/tags");
        Poco::Net::HTTPClientSession sess(uri.getHost(), uri.getPort());
        sess.setTimeout(Poco::Timespan(10, 0));

        Poco::Net::HTTPRequest req(Poco::Net::HTTPRequest::HTTP_GET,
                                   uri.getPathAndQuery(),
                                   Poco::Net::HTTPMessage::HTTP_1_1);
        sess.sendRequest(req);

        Poco::Net::HTTPResponse resp;
        std::istream& in = sess.receiveResponse(resp);

        if (resp.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
            if (!cancelled()) {
                auto* ev = new wxCommandEvent(wxEVT_MM_LOAD_ERROR);
                ev->SetString("HTTP " + std::to_string(resp.getStatus()));
                SafePost(ev);
            }
            return (ExitCode)0;
        }

        std::string body;
        Poco::StreamCopier::copyToString(in, body);
        if (cancelled()) return (ExitCode)0;

        Poco::JSON::Parser parser;
        auto obj = parser.parse(body).extract<Poco::JSON::Object::Ptr>();

        wxString packed;
        if (obj->has("models")) {
            auto arr = obj->getArray("models");
            for (size_t i = 0; i < arr->size(); i++) {
                if (cancelled()) return (ExitCode)0;
                auto m = arr->getObject(i);
                std::string name = m->optValue<std::string>("name", "?");
                int64_t size = m->optValue<int64_t>("size", -1);
                std::string quant;
                if (m->has("details")) {
                    auto det = m->getObject("details");
                    quant = det->optValue<std::string>("quantization_level", "");
                }
                if (!packed.empty()) packed += "\n";
                packed += wxString::FromUTF8(name) + "\t"
                        + wxString::FromUTF8(FormatSize(size)) + "\t"
                        + wxString::FromUTF8(quant);
            }
        }

        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_MODELS_LOADED);
            ev->SetString(packed);
            SafePost(ev);
        }
    }
    catch (const Poco::Exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_LOAD_ERROR);
            ev->SetString(wxString::FromUTF8(ex.displayText()));
            SafePost(ev);
        }
    }
    catch (const std::exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_LOAD_ERROR);
            ev->SetString(wxString::FromUTF8(ex.what()));
            SafePost(ev);
        }
    }
    return (ExitCode)0;
}

// ═══════════════════════════════════════════════════════════════════
//  MMDeleteThread
// ═══════════════════════════════════════════════════════════════════

MMDeleteThread::MMDeleteThread(wxEvtHandler* handler, const std::string& apiUrl,
                               const std::string& modelName,
                               std::shared_ptr<std::atomic<bool>> cancelFlag)
    : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_apiUrl(apiUrl),
      m_modelName(modelName), m_cancelFlag(cancelFlag)
{}

bool MMDeleteThread::SafePost(wxCommandEvent* ev)
{
    if (m_cancelFlag->load()) { delete ev; return false; }
    wxQueueEvent(m_handler, ev);
    return true;
}

wxThread::ExitCode MMDeleteThread::Entry()
{
    auto cancelled = [this]() { return m_cancelFlag->load(); };
    try {
        Poco::URI uri(m_apiUrl + "/api/delete");
        Poco::Net::HTTPClientSession sess(uri.getHost(), uri.getPort());
        sess.setTimeout(Poco::Timespan(30, 0));

        Poco::Net::HTTPRequest req(Poco::Net::HTTPRequest::HTTP_DELETE,
                                   uri.getPathAndQuery(),
                                   Poco::Net::HTTPMessage::HTTP_1_1);
        req.setContentType("application/json");

        Poco::JSON::Object body;
        body.set("name", m_modelName);
        std::ostringstream oss;
        body.stringify(oss);
        std::string jsonStr = oss.str();
        req.setContentLength(jsonStr.size());

        sess.sendRequest(req) << jsonStr;

        Poco::Net::HTTPResponse resp;
        std::istream& in = sess.receiveResponse(resp);
        std::string respBody;
        Poco::StreamCopier::copyToString(in, respBody);

        if (cancelled()) return (ExitCode)0;

        if (resp.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
            auto* ev = new wxCommandEvent(wxEVT_MM_DELETE_OK);
            ev->SetString(wxString::FromUTF8(m_modelName));
            SafePost(ev);
        } else {
            auto* ev = new wxCommandEvent(wxEVT_MM_DELETE_ERROR);
            ev->SetString(wxString::Format("HTTP %d: %s",
                          (int)resp.getStatus(), wxString::FromUTF8(respBody)));
            SafePost(ev);
        }
    }
    catch (const Poco::Exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_DELETE_ERROR);
            ev->SetString(wxString::FromUTF8(ex.displayText()));
            SafePost(ev);
        }
    }
    catch (const std::exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_DELETE_ERROR);
            ev->SetString(wxString::FromUTF8(ex.what()));
            SafePost(ev);
        }
    }
    return (ExitCode)0;
}

// ═══════════════════════════════════════════════════════════════════
//  MMPullThread — streams JSON progress from POST /api/pull
// ═══════════════════════════════════════════════════════════════════

MMPullThread::MMPullThread(wxEvtHandler* handler, const std::string& apiUrl,
                           const std::string& modelName,
                           std::shared_ptr<std::atomic<bool>> cancelFlag)
    : wxThread(wxTHREAD_DETACHED), m_handler(handler), m_apiUrl(apiUrl),
      m_modelName(modelName), m_cancelFlag(cancelFlag)
{}

bool MMPullThread::SafePost(wxCommandEvent* ev)
{
    if (m_cancelFlag->load()) { delete ev; return false; }
    wxQueueEvent(m_handler, ev);
    return true;
}

wxThread::ExitCode MMPullThread::Entry()
{
    auto cancelled = [this]() { return m_cancelFlag->load(); };
    try {
        Poco::URI uri(m_apiUrl + "/api/pull");
        Poco::Net::HTTPClientSession sess(uri.getHost(), uri.getPort());
        // Long timeout — large model pulls can take minutes
        sess.setTimeout(Poco::Timespan(600, 0));

        Poco::Net::HTTPRequest req(Poco::Net::HTTPRequest::HTTP_POST,
                                   uri.getPathAndQuery(),
                                   Poco::Net::HTTPMessage::HTTP_1_1);
        req.setContentType("application/json");

        Poco::JSON::Object body;
        body.set("name", m_modelName);
        std::ostringstream oss;
        body.stringify(oss);
        std::string jsonStr = oss.str();
        req.setContentLength(jsonStr.size());

        sess.sendRequest(req) << jsonStr;

        Poco::Net::HTTPResponse resp;
        std::istream& in = sess.receiveResponse(resp);

        if (resp.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
            std::string errBody;
            Poco::StreamCopier::copyToString(in, errBody);
            if (!cancelled()) {
                auto* ev = new wxCommandEvent(wxEVT_MM_PULL_ERROR);
                ev->SetString(wxString::Format("HTTP %d: %s",
                              (int)resp.getStatus(), wxString::FromUTF8(errBody)));
                SafePost(ev);
            }
            return (ExitCode)0;
        }

        // Stream newline-delimited JSON objects
        // Each line: {"status":"pulling ...","digest":"sha256:...","total":N,"completed":N}
        // Final line: {"status":"success"}
        std::string line;
        while (std::getline(in, line)) {
            if (cancelled()) return (ExitCode)0;
            if (line.empty()) continue;
            // Trim trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            try {
                Poco::JSON::Parser p;
                auto obj = p.parse(line).extract<Poco::JSON::Object::Ptr>();

                std::string status = obj->optValue<std::string>("status", "");

                if (status == "success") {
                    if (!cancelled()) {
                        auto* ev = new wxCommandEvent(wxEVT_MM_PULL_COMPLETE);
                        ev->SetString(wxString::FromUTF8(m_modelName));
                        SafePost(ev);
                    }
                    return (ExitCode)0;
                }

                // Calculate percentage if total/completed present
                int pct = -1;
                if (obj->has("total") && obj->has("completed")) {
                    int64_t total = obj->getValue<int64_t>("total");
                    int64_t completed = obj->getValue<int64_t>("completed");
                    if (total > 0) {
                        pct = static_cast<int>((completed * 100) / total);
                        if (pct > 100) pct = 100;
                    }
                }

                // Build display string
                wxString display;
                if (pct >= 0)
                    display = wxString::Format("%s (%d%%)", status, pct);
                else
                    display = wxString::FromUTF8(status);

                auto* ev = new wxCommandEvent(wxEVT_MM_PULL_PROGRESS);
                ev->SetInt(pct);
                ev->SetString(display);
                SafePost(ev);
            }
            catch (...) {
                // Ignore unparseable lines
            }
        }

        // Stream ended without "success" — might still be OK
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_PULL_COMPLETE);
            ev->SetString(wxString::FromUTF8(m_modelName));
            SafePost(ev);
        }
    }
    catch (const Poco::Exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_PULL_ERROR);
            ev->SetString(wxString::FromUTF8(ex.displayText()));
            SafePost(ev);
        }
    }
    catch (const std::exception& ex) {
        if (!cancelled()) {
            auto* ev = new wxCommandEvent(wxEVT_MM_PULL_ERROR);
            ev->SetString(wxString::FromUTF8(ex.what()));
            SafePost(ev);
        }
    }
    return (ExitCode)0;
}
