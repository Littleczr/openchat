// modelfile_creator.h
// Dialog for creating custom Ollama models via POST /api/create.
// Provides a form-based UI for FROM, SYSTEM, and PARAMETER fields.

#pragma once

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/spinctrl.h>
#include <wx/gauge.h>
#include <wx/thread.h>
#include <string>
#include <vector>
#include <memory>
#include <atomic>

// Forward declarations
struct ThemeData;

// ── Custom events ─────────────────────────────────────────────────
wxDECLARE_EVENT(wxEVT_MC_CREATE_PROGRESS, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MC_CREATE_COMPLETE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_MC_CREATE_ERROR,    wxCommandEvent);

// ── Thread: create model (streaming) ──────────────────────────────
class MCCreateThread : public wxThread
{
public:
    MCCreateThread(wxEvtHandler* handler, const std::string& apiUrl,
                   const std::string& requestJson,
                   std::shared_ptr<std::atomic<bool>> cancelFlag);
protected:
    ExitCode Entry() override;
private:
    wxEvtHandler* m_handler;
    std::string   m_apiUrl;
    std::string   m_requestJson;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    bool SafePost(wxCommandEvent* ev);
};

// ── Modelfile Creator dialog ──────────────────────────────────────
class ModelfileCreatorDialog : public wxDialog
{
public:
    ModelfileCreatorDialog(wxWindow* parent, const std::string& apiUrl,
                           const std::vector<std::string>& installedModels,
                           const ThemeData* theme = nullptr);
    ~ModelfileCreatorDialog();

    // True if a model was successfully created
    bool WasModelCreated() const { return m_created; }

private:
    void CreateControls();
    void ApplyTheme();
    std::string BuildRequestJson() const;

    // Event handlers
    void OnCreate(wxCommandEvent& ev);
    void OnCreateProgress(wxCommandEvent& ev);
    void OnCreateComplete(wxCommandEvent& ev);
    void OnCreateError(wxCommandEvent& ev);
    void OnPreview(wxCommandEvent& ev);
    void OnCancel(wxCommandEvent& ev);

    // ── Widgets ───────────────────────────────────────────────────
    wxTextCtrl*     m_modelNameCtrl;      // Name for the new model
    wxComboBox*     m_baseModelCombo;     // FROM model
    wxTextCtrl*     m_systemPromptCtrl;   // SYSTEM message
    wxTextCtrl*     m_temperatureCtrl;    // PARAMETER temperature
    wxTextCtrl*     m_numCtxCtrl;         // PARAMETER num_ctx
    wxTextCtrl*     m_topKCtrl;           // PARAMETER top_k
    wxTextCtrl*     m_topPCtrl;           // PARAMETER top_p
    wxTextCtrl*     m_repeatPenaltyCtrl;  // PARAMETER repeat_penalty
    wxTextCtrl*     m_seedCtrl;           // PARAMETER seed
    wxTextCtrl*     m_stopCtrl;           // PARAMETER stop (comma-separated)
    wxButton*       m_createButton;
    wxButton*       m_previewButton;
    wxGauge*        m_progressBar;
    wxStaticText*   m_statusText;

    // ── State ─────────────────────────────────────────────────────
    std::string m_apiUrl;
    std::vector<std::string> m_installedModels;
    const ThemeData* m_theme;
    bool m_created;
    bool m_busy;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;

    wxDECLARE_EVENT_TABLE();
};

