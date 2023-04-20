// -*- c++ -*-
#pragma once

#include <cassert>
#include <cstring>
#include <iostream>
#include <functional>
#include <optional>
#include <vector>

#include <ext/stdio_filebuf.h>
#include <microhttpd.h>

//
// Simple C++ interface to microhttpd
//
class Httpd {
public:
  bool start() {
    daemon_ = MHD_start_daemon(MHD_USE_DEBUG, 8080, NULL, NULL,
                               &s_HandlerCb, this, MHD_OPTION_END);
    return daemon_ != nullptr;
  }

  bool run_wait(int millis) {
    return MHD_YES == MHD_run_wait(daemon_, 1000);
  }

  void stop() {
    MHD_stop_daemon(daemon_);
  }

  class Request {
  public:
    Request(MHD_Connection *connection, const char* url) :
      connection_(connection),
      url_(url) { }

    std::optional<std::string> arg(const std::string& key) {
      bool val;
      std::string str;

      const char* c_str = MHD_lookup_connection_value(connection_,
                                                      MHD_GET_ARGUMENT_KIND,
                                                      key.c_str());
      if (c_str)
        return std::string(c_str);
      else
        return std::nullopt;
    }

    ~Request() {
      MHD_destroy_response(response_);
      delete os_;
      delete sb_;
    }

    const std::string& url() const { return url_; }

    std::ostream& os() {
      if (response_ == nullptr) {
        int pipefds[2];
        pipe(pipefds);
        sb_ = new __gnu_cxx::stdio_filebuf<char>(pipefds[1], std::ios_base::out);
        os_ = new std::ostream(sb_);
        response_ = MHD_create_response_from_pipe(pipefds[0]);

        MHD_add_response_header(response_, MHD_HTTP_HEADER_CONTENT_TYPE, content_type_.c_str());
        MHD_queue_response(connection_, response_code_, response_);
      }
      return *os_;
    }

    void set_response_code(int resp_code) {
      assert(response_ == nullptr);
      response_code_ = resp_code;
    }

    void set_content_type(std::string&& content_type) {
      assert(response_ == nullptr);
      content_type_ = std::move(content_type);
    }

  private:
    MHD_Connection *connection_;
    std::string url_;

    MHD_Response *response_ = nullptr;
    int output_pipefd;
    std::streambuf* sb_ = nullptr;
    std::ostream* os_ = nullptr;

    int response_code_ = 200;
    std::string content_type_ = "text/html";
  };

  using Handler = std::function<bool(Request&)>;

  Httpd& add(Handler&& handler) {
    handlers_.push_back(std::move(handler));
    return *this;
  }

  // Adds a handler which gets called when url matches.
  Httpd& add(const std::string& url, Handler&& handler) {
    handlers_.push_back([url, h = std::move(handler)](Request& req) {
      if (url == req.url()) {
        return h(req);
      }
      else {
        return true;
      }
    });
    return *this;
  }

private:
  std::vector<Handler> handlers_;

  static MHD_Result s_HandlerCb(void *cls,
                                struct MHD_Connection *connection,
                                const char *url,
                                const char *method,
                                const char *version,
                                const char *upload_data,
                                size_t *upload_data_size,
                                void **con_cls) {
    return reinterpret_cast<Httpd *>(cls)->HandlerCb(connection, url, method,
                                                     version, upload_data,
                                                     upload_data_size, con_cls);
  }

  MHD_Result HandlerCb(struct MHD_Connection *connection,
                       const char *url,
                       const char *method,
                       const char *version,
                       const char *upload_data,
                       size_t *upload_data_size,
                       void **con_cls) {
    MHD_Result ret;
    static const std::string hello { "<body>hello</body>\n" };

    if (nullptr == *con_cls) {
      Request req(connection, url);
      for (auto &h : handlers_) {
        h(req);
      }
      ret = MHD_YES;

      static int x = 1;
      *con_cls = &x;

      return ret;
    }

    std::cout << "(ignored)" << method << " " << url << " " << version << std::endl;

    return MHD_YES;
  }

  MHD_Daemon *daemon_;
};
