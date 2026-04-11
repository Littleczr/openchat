// chat_client.cpp
#include "chat_client.h"
#include <Poco/JSON/JSONException.h>

// Poco headers for HTTP communication
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
#include <Poco/JSON/Stringifier.h>
#include <Poco/Exception.h>

#include <sstream>

// Define custom events
wxDEFINE_EVENT(wxEVT_ASSISTANT_DELTA, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_ASSISTANT_COMPLETE, wxCommandEvent);
wxDEFINE_EVENT(wxEVT_ASSISTANT_ERROR, wxCommandEvent);

// ═══════════════════════════════════════════════════════════════════
// ChatWorkerThread Implementation
// ═══════════════════════════════════════════════════════════════════

ChatWorkerThread::ChatWorkerThread(wxEvtHandler* eventHandler,
    const std::string& model,
    const std::string& apiUrl,
    const std::string& requestBody,
    std::shared_ptr<std::atomic<bool>> cancelFlag,
    std::weak_ptr<std::atomic<bool>> aliveToken,
    unsigned long generationId)
    : wxThread(wxTHREAD_DETACHED)
    , m_eventHandler(eventHandler)
    , m_model(model)
    , m_apiUrl(apiUrl)
    , m_requestBody(requestBody)
    , m_cancelFlag(cancelFlag)
    , m_aliveToken(aliveToken)
    , m_generationId(generationId)
{
}

// ── Safe event posting ──────────────────────────────────
// Checks that the event handler's owner (MyFrame) is still alive
// before posting.  If the owner is gone the event is deleted and
// we return false so the caller knows to bail out.
bool ChatWorkerThread::SafeQueueEvent(wxCommandEvent* event)
{
    auto alive = m_aliveToken.lock();
    if (!alive || !alive->load()) {
        delete event;   // owner is gone — discard event
        return false;
    }
    // Stamp every event with the generation ID so the
    // handler can discard stale events from a previous request.
    event->SetExtraLong(m_generationId);
    wxQueueEvent(m_eventHandler, event);
    return true;
}

wxThread::ExitCode ChatWorkerThread::Entry()
{
    std::string fullReply;

    // Local helper: check if cancellation was requested.
    // Uses the shared atomic flag instead of TestDestroy(), which avoids
    // any need for the main thread to hold a pointer to this detached thread.
    auto isCancelled = [this]() { return m_cancelFlag->load(); };

    try {
        Poco::URI uri(m_apiUrl + "/api/chat");
        Poco::Net::HTTPClientSession sess(uri.getHost(), uri.getPort());
        sess.setTimeout(Poco::Timespan(30, 0)); // 30 second timeout

        Poco::Net::HTTPRequest req(
            Poco::Net::HTTPRequest::HTTP_POST,
            uri.getPathAndQuery(),
            Poco::Net::HTTPMessage::HTTP_1_1
        );
        req.setContentType("application/json");
        req.setContentLength((long)m_requestBody.size());

        std::ostream& out = sess.sendRequest(req);
        out << m_requestBody;
        out.flush();

        Poco::Net::HTTPResponse resp;
        std::istream& in = sess.receiveResponse(resp);

        if (resp.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
            std::string err;
            Poco::StreamCopier::copyToString(in, err);

            // Post error via SafeQueueEvent
            wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_ERROR);
            event->SetString(wxString::FromUTF8(
                "API Error: " + resp.getReason() + " - " + err
            ));
            SafeQueueEvent(event);
            return (ExitCode)0;
        }

        Poco::JSON::Parser parser;
        std::string line;

        while (std::getline(in, line) && !isCancelled()) {
            // Strip trailing \r (HTTP streams may use \r\n line endings)
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty()) continue;

            try {
                auto obj = parser.parse(line).extract<Poco::JSON::Object::Ptr>();
                if (obj->has("message")) {
                    auto msgObj = obj->getObject("message");
                    if (msgObj->has("content")) {
                        std::string delta = msgObj->getValue<std::string>("content");
                        fullReply += delta;

                        // Post delta via SafeQueueEvent
                        wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_DELTA);
                        event->SetString(wxString::FromUTF8(delta));
                        if (!SafeQueueEvent(event))
                            return (ExitCode)0;  // owner gone — stop immediately
                    }
                }
                if (obj->has("done") && obj->getValue<bool>("done"))
                    break;
            }
            catch (const Poco::JSON::JSONException&) {
                // Skip malformed JSON lines
                continue;
            }
        }

        if (!isCancelled()) {
            // Post completion via SafeQueueEvent
            wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_COMPLETE);
            event->SetString(wxString::FromUTF8(fullReply));
            SafeQueueEvent(event);
        }
    }
    catch (const Poco::Net::HTTPException& ex) {
        if (!isCancelled()) {
            wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_ERROR);
            event->SetString(wxString::FromUTF8("HTTP Error: " + ex.displayText()));
            SafeQueueEvent(event);
        }
    }
    catch (const Poco::Net::NetException& ex) {
        if (!isCancelled()) {
            wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_ERROR);
            event->SetString(wxString::FromUTF8("Network Error: " + ex.displayText()));
            SafeQueueEvent(event);
        }
    }
    catch (const Poco::Exception& ex) {
        if (!isCancelled()) {
            wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_ERROR);
            event->SetString(wxString::FromUTF8("Poco Error: " + ex.displayText()));
            SafeQueueEvent(event);
        }
    }
    catch (const std::exception& ex) {
        if (!isCancelled()) {
            wxCommandEvent* event = new wxCommandEvent(wxEVT_ASSISTANT_ERROR);
            event->SetString(wxString::FromUTF8(std::string("Error: ") + ex.what()));
            SafeQueueEvent(event);
        }
    }

    return (ExitCode)0;
}

// ═══════════════════════════════════════════════════════════════════
// ChatClient Implementation
// ═══════════════════════════════════════════════════════════════════

// Constructor now stores a weak liveness token
ChatClient::ChatClient(wxEvtHandler* eventHandler,
                       std::weak_ptr<std::atomic<bool>> aliveToken)
    : m_eventHandler(eventHandler)
    , m_aliveToken(aliveToken)
    , m_isStreaming(false)
{
}

ChatClient::~ChatClient()
{
    StopGeneration();
}

// SendMessage now takes a generationId to pass through
bool ChatClient::SendMessage(const std::string& model,
    const std::string& apiUrl,
    const std::string& requestBody,
    unsigned long generationId)
{
    if (m_isStreaming) {
        return false; // Already streaming
    }

    m_isStreaming = true;
    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);

    auto* thread = new ChatWorkerThread(
        m_eventHandler, model, apiUrl, requestBody,
        m_cancelFlag, m_aliveToken, generationId);

    if (thread->Run() != wxTHREAD_NO_ERROR) {
        // Thread not yet started — safe to delete manually
        delete thread;
        m_cancelFlag.reset();
        m_isStreaming = false;
        return false;
    }

    // Thread is now detached and running — we do NOT store its pointer.
    // Cancellation is communicated via m_cancelFlag.
    return true;
}

void ChatClient::StopGeneration()
{
    if (m_isStreaming && m_cancelFlag) {
        m_cancelFlag->store(true);  // Signal the thread to stop
        m_cancelFlag.reset();
        m_isStreaming = false;
    }
}

void ChatClient::ResetStreamingState()
{
    m_isStreaming = false;
    m_cancelFlag.reset();
}

bool ChatClient::UnloadModel(const std::string& modelName,
    const std::string& apiUrl,
    Poco::Logger* logger)
{
    if (modelName.empty() || apiUrl.empty()) {
        return false;
    }

    if (logger) {
        logger->information("Unloading model: " + modelName);
    }

    try {
        // Create request to unload model using keep_alive: 0
        Poco::JSON::Object::Ptr req(new Poco::JSON::Object);
        req->set("model", modelName);
        req->set("prompt", "");  // Empty prompt
        req->set("stream", false);
        req->set("keep_alive", 0);  // This immediately unloads the model

        std::ostringstream oss;
        Poco::JSON::Stringifier::stringify(req, oss);
        std::string body = oss.str();

        Poco::URI uri(apiUrl + "/api/generate");
        Poco::Net::HTTPClientSession sess(uri.getHost(), uri.getPort());
        sess.setTimeout(Poco::Timespan(5, 0)); // 5 second timeout

        Poco::Net::HTTPRequest r(
            Poco::Net::HTTPRequest::HTTP_POST,
            uri.getPathAndQuery(),
            Poco::Net::HTTPMessage::HTTP_1_1
        );
        r.setContentType("application/json");
        r.setContentLength((long)body.size());

        std::ostream& out = sess.sendRequest(r);
        out << body;
        out.flush();

        Poco::Net::HTTPResponse resp;
        std::istream& in = sess.receiveResponse(resp);
        std::string respBody;
        Poco::StreamCopier::copyToString(in, respBody);

        if (resp.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
            if (logger) {
                logger->information("Successfully unloaded model: " + modelName);
            }
            return true;
        }
        else {
            if (logger) {
                logger->warning("Failed to unload model " + modelName +
                    ", status: " + std::to_string(resp.getStatus()));
            }
            return false;
        }
    }
    catch (const Poco::Exception& ex) {
        if (logger) {
            logger->error("Error unloading model " + modelName + ": " + ex.displayText());
        }
        return false;
    }
}
