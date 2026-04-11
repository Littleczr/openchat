// model_manager.h
#pragma once

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/gauge.h>
#include <wx/thread.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

// ── Custom events ─────────────────────────────────────────────────
wxDECLARE_EVENT(wxEVT_MM_MODELS_LOADED,   wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MM_LOAD_ERROR,      wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MM_DELETE_OK,        wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MM_DELETE_ERROR,     wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MM_PULL_PROGRESS,    wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MM_PULL_COMPLETE,    wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MM_PULL_ERROR,       wxCommandEvent);

// ── Thread: list models ───────────────────────────────────────────
class MMListThread : public wxThread
{
public:
    MMListThread(wxEvtHandler* handler, const std::string& apiUrl,
                 std::shared_ptr<std::atomic<bool>> cancelFlag);
protected:
    ExitCode Entry() override;
private:
    wxEvtHandler* m_handler;
    std::string   m_apiUrl;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    bool SafePost(wxCommandEvent* ev);
};

// ── Thread: delete model ──────────────────────────────────────────
class MMDeleteThread : public wxThread
{
public:
    MMDeleteThread(wxEvtHandler* handler, const std::string& apiUrl,
                   const std::string& modelName,
                   std::shared_ptr<std::atomic<bool>> cancelFlag);
protected:
    ExitCode Entry() override;
private:
    wxEvtHandler* m_handler;
    std::string   m_apiUrl;
    std::string   m_modelName;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    bool SafePost(wxCommandEvent* ev);
};

// ── Thread: pull model (streaming progress) ───────────────────────
class MMPullThread : public wxThread
{
public:
    MMPullThread(wxEvtHandler* handler, const std::string& apiUrl,
                 const std::string& modelName,
                 std::shared_ptr<std::atomic<bool>> cancelFlag);
protected:
    ExitCode Entry() override;
private:
    wxEvtHandler* m_handler;
    std::string   m_apiUrl;
    std::string   m_modelName;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    bool SafePost(wxCommandEvent* ev);
};

// Forward declarations
struct ThemeData;

// ── Model Manager dialog ──────────────────────────────────────────
class ModelManagerDialog : public wxDialog
{
public:
    ModelManagerDialog(wxWindow* parent, const std::string& apiUrl,
                       const ThemeData* theme = nullptr);
    ~ModelManagerDialog();

private:
    void CreateControls();
    void StartListModels();
    void SetBusy(bool busy);

    // Event handlers
    void OnModelsLoaded(wxCommandEvent& ev);
    void OnLoadError(wxCommandEvent& ev);
    void OnDeleteClicked(wxCommandEvent& ev);
    void OnDeleteOK(wxCommandEvent& ev);
    void OnDeleteError(wxCommandEvent& ev);
    void OnPullClicked(wxCommandEvent& ev);
    void OnPullProgress(wxCommandEvent& ev);
    void OnPullComplete(wxCommandEvent& ev);
    void OnPullError(wxCommandEvent& ev);
    void OnRefreshClicked(wxCommandEvent& ev);
    void OnCreateClicked(wxCommandEvent& ev);
    void OnClose(wxCommandEvent& ev);

    wxListCtrl*   m_modelList;
    wxTextCtrl*   m_pullInput;
    wxButton*     m_pullButton;
    wxButton*     m_deleteButton;
    wxButton*     m_refreshButton;
    wxGauge*      m_progressBar;
    wxStaticText* m_statusText;

    std::string m_apiUrl;
    bool        m_busy;
    const ThemeData* m_theme;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;

    wxDECLARE_EVENT_TABLE();
};

