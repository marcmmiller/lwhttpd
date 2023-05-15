// -*- c++ -*-
#pragma once

#include <cassert>
#include <cstring>
#include <iostream>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <queue>
#include <variant>
#include <vector>

#include <ext/stdio_filebuf.h>
#include <microhttpd.h>
#include <sys/eventfd.h>

//
// microhttpd is a fever dream of an API.  This modern C++ wrapper (hopefully)
// hides all the unpleasantries and delivers an experience more like express,
// and like express it does so without using multi-threading, which is what
// other libraries do.
//
class Httpd {
public:
  Httpd() : msgloop_(this) {}

  bool start(int port = 8080) {
    daemon_ = MHD_start_daemon(MHD_USE_DEBUG | MHD_ALLOW_SUSPEND_RESUME, port,
                               NULL, NULL, &s_HandlerCb, this, MHD_OPTION_END);
    return daemon_ != nullptr;
  }

  class MhdMessageloop {
  public:
    MhdMessageloop(Httpd* parent) {
      parent_ = parent;
      efd_ = eventfd(0, 0);
    }

    bool run() {
      do {
        fd_set read_fds, write_fds, except_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_ZERO(&except_fds);
        MHD_socket max;
        if (MHD_YES !=
            MHD_get_fdset(parent_->daemon_, &read_fds, &write_fds, &except_fds, &max))
          break;

        FD_SET(efd_, &read_fds);

        struct timeval tv = { 0 };
        MHD_UNSIGNED_LONG_LONG mhd_timeout;
        if (MHD_get_timeout(parent_->daemon_, &mhd_timeout) == MHD_YES) {
          if (((MHD_UNSIGNED_LONG_LONG)tv.tv_sec) < mhd_timeout / 1000LL) {
            tv.tv_sec = mhd_timeout / 1000LL;
            tv.tv_usec = (mhd_timeout - (tv.tv_sec * 1000LL)) * 1000LL;
          }
        }

        if (-1 ==
            select(max + 1, &read_fds, &write_fds, &except_fds, &tv)) {
          if (EINTR != errno) abort();
        }

        if (FD_ISSET(efd_, &read_fds)) {
          std::cout << "EFD is set! (you'll probably see this repeat until I read() from it)" << std::endl;
        }

        if (MHD_YES != MHD_run_from_select(parent_->daemon_, &read_fds,
                                           &write_fds, &except_fds))
          break;
      } while (true);

      return true;
    }

    Httpd* parent_;
    std::queue<std::function<bool()>> q_;
    int efd_;
  };

  MhdMessageloop& msgloop() { return msgloop_; }

  bool run() {
#ifdef RUN_WAIT
    do { MHD_run_wait(daemon_, 9999); } while (true);
    return true;
#else
    return msgloop_.run();
#endif
  }

  void stop() {
    MHD_stop_daemon(daemon_);
  }

  class Request {
  public:
    Request(MHD_Connection *connection, const std::string& url, const std::string& method) :
      connection_(connection),
      url_(url),
      method_(method)
    { }

    std::optional<std::string> arg(const std::string& key) {
      bool val;
      std::string str;

      const char* c_str = MHD_lookup_connection_value(connection_,
                                                      MHD_GET_ARGUMENT_KIND,
                                                      key.c_str());
      if (c_str)
        return std::string(c_str);
      else if (auto it = post_data_.find(key); it != post_data_.end())
        return it->second;
      else
        return std::nullopt;
    }

    ~Request() {
      MHD_destroy_response(response_);
      delete os_;
      delete sb_;
      MHD_resume_connection(connection_);
    }

    const std::string& url() const { return url_; }
    const std::string& method() const { return method_; }

    // TODO: if this method isn't called then the response is never created and
    // it's infinite loop time
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

    void create_post_processor() {
      // Uncounted ref to request should be fine since the post processor
      // outlives the connection.
      pp_ = MHD_create_post_processor(connection_, 4096, s_PostProcessorCb, this);
    }

    void post_process(const char *post_data, size_t post_data_len) {
      if (0 != post_data_len) {
        MHD_post_process(pp_, post_data, post_data_len);
      }
      else {
        MHD_destroy_post_processor(pp_);
      }
    }

  private:
    friend class Httpd;
    MHD_Connection *connection_;
    std::string url_;
    std::string method_;

    MHD_Response *response_ = nullptr;
    MHD_PostProcessor *pp_ = nullptr;
    int output_pipefd;
    std::streambuf *sb_ = nullptr;
    std::ostream *os_ = nullptr;

    int response_code_ = 200;
    std::string content_type_ = "text/html";

    std::map<std::string, std::string> post_data_;

    static MHD_Result s_PostProcessorCb(void *cls, enum MHD_ValueKind kind,
                                        const char *key, const char *filename,
                                        const char *content_type,
                                        const char *transfer_encoding,
                                        const char *data, uint64_t off,
                                        size_t size) {
      return reinterpret_cast<Request *>(cls)->PostProcessorCb(
          kind, key, filename, content_type, transfer_encoding, data, off,
          size);
    }

    MHD_Result PostProcessorCb(enum MHD_ValueKind kind, std::string key,
                               const char *filename, const char *content_type,
                               const char *transfer_encoding, const char *data,
                               uint64_t off, size_t size) {
      auto it = post_data_.find(key);
      if (it != post_data_.end()) {
        auto& val = it->second;
        val.reserve(off + size + 1);
        val.replace(off, size, data, size);
      }
      else {
        assert(off == 0);
        post_data_.emplace(key, std::string(data, size));
      }
      return MHD_YES;
    }
  };

  using Handler = std::function<bool(Request&)>;
  using HandlerAsync = std::function<bool(std::shared_ptr<Request>)>;

  Httpd& use(Handler&& handler) {
    handlers_.push_back(std::move(handler));
    return *this;
  }

  // Adds a handler which gets called when url matches.
  Httpd& use(const std::string& url, Handler&& handler) {
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
  std::vector<std::variant<Handler, HandlerAsync>> handlers_;

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
                       std::string url,
                       std::string method,
                       std::string version,
                       const char *upload_data,
                       size_t *upload_data_size,
                       void **con_cls) {
    MHD_Result ret;
    std::shared_ptr<Request> req;
    bool is_post = method == MHD_HTTP_METHOD_POST;

    if (nullptr == *con_cls) {
      req = std::make_shared<Request>(connection, url, method);

      if (is_post) {
        req->create_post_processor();
      }

      // This is deallocated in the connection ended callback
      auto *preq = new std::shared_ptr<Request>(req);
      *con_cls = preq;

      return MHD_YES;
    }
    else {
      req = *reinterpret_cast<std::shared_ptr<Request> *>(*con_cls);
    }

    if (is_post) {
      req->post_process(upload_data, *upload_data_size);
      if (0 == *upload_data_size) {
        // Indicates that we've processed all data.
        *upload_data_size = 0;
        return MHD_YES;
      }
    }

    for (auto &h : handlers_) {
      if (std::holds_alternative<Handler>(h)) {
        std::get<Handler>(h)(*req);
      } else {
        std::get<HandlerAsync>(h)(req);
      }
    }

    MHD_suspend_connection(connection);

    req.reset();
    delete reinterpret_cast<std::shared_ptr<Request> *>(*con_cls);
    *con_cls = nullptr;

    return MHD_YES;
  }

    MhdMessageloop msgloop_;
    MHD_Daemon *daemon_;
  };
