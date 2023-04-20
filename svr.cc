#include "httpd.h"

#include <iostream>
#include <map>

using namespace std;

int main(int argc, char *argv[]) {
  std::map<std::string, std::string> map;

  Httpd h;
  if (!h.start()) {
    cerr << "couldn't start daemon\n";
    return 1;
  }

  h.add([&](Httpd::Request& req) {
    cout << "logging " << req.url() << endl;
    return true;
  });

  h.add("/put", [&](Httpd::Request& req) {
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

  h.add("/get", [&](Httpd::Request& req) {
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

  printf("running...\n");

  for (bool b = true; b = h.run_wait(1000); b)
    ;

  h.stop();
  return 0;
}
