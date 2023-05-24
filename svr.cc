#include "httpd.h"

#include <iostream>
#include <map>

using namespace std;

//
// Sample (test case) for server.
//
int main(int argc, char *argv[]) {
  std::map<std::string, std::string> map;

  Httpd h;
  if (!h.start()) {
    cerr << "couldn't start daemon\n";
    return 1;
  }

  h.use([&](Httpd::Request& req) {
    cout << "logging " << req.url() << endl;
    return true;
  });

  h.use("/", [&](Httpd::Request& req) {
    req.os() << "response\n";
    return true;
  });

  h.use("/delay", [&](std::shared_ptr<Httpd::Request> req) {
    h.event_loop().set_timer(1000ms, [=] {
      req->os() << "delayed response!\n";
      return true;
    });
    return true;
  });

  h.use("/put", [&](Httpd::Request& req) {
    cout << "put!!" << endl;
    auto key = req.arg("key");
    auto val = req.arg("val");
    req.os() << "This is the <b>best</b> httpd class ever." << endl
             << "<p/>" << endl
             << "key = " << key.value_or("") << "<br/>" << endl
             << "val = " << val.value_or("") << "<br/>" << endl;

    if (key && val) {
      map[*key] = *val;
    }

    return true;
  });

  h.use("/get", [&](Httpd::Request& req) {
    auto key = req.arg("key");

    if (auto f = map.find(*key); key && f != map.end()) {
        req.set_content_type("text/plain");
        req.os() << f->second;
    }
    else {
      req.set_response_code(404);
      req.os() << "Not found" << endl;
    }

    return true;
  });

  using namespace std::chrono_literals;
  h.event_loop().set_timer(3000ms, [] {
    std::cout << "hello from 3s callback land\n";
    return true;
  });

  h.event_loop().set_timer(0ms, [] {
    std::cout << "hello from immediate callback land\n";
    return true;
  });


  std::cout << "running...\n" << std::flush;

  h.run();

  h.stop();
  return 0;
}
