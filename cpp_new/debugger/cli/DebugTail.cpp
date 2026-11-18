#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// krkr_debug：调试事件文件的最小前端。
//
//   krkr_debug --summary <events.jsonl>
//   krkr_debug --filter <key=value[,key=value]> <events.jsonl> [--raw]
//
// --filter 命中任意一条即退出码 0，否则 1，便于在 CTest 里断言"某类事件出现过"。
// 事件是 EventRecorder 写出的单层扁平 JSON，本文件只解析这一种形状，不追求通用。

namespace {

struct Event {
  std::string raw;
  std::map<std::string, std::string> fields;
};

class Scanner {
  public:
    explicit Scanner(const std::string &text) : text_(text) {}

    bool AtEnd() const { return index_ >= text_.size(); }

    void Skip(char expected) {
      while (!AtEnd() && (text_[index_] == ' ' || text_[index_] == ',')) {
        ++index_;
      }
      if (!AtEnd() && text_[index_] == expected) {
        ++index_;
      }
    }

    std::string ReadKey() {
      Skip('"');
      return ReadQuoted();
    }

    std::string ReadValue() {
      Skip(' ');
      if (AtEnd()) {
        return {};
      }
      if (text_[index_] == '"') {
        ++index_;
        return Unescape(ReadQuoted());
      }
      const std::size_t start = index_;
      while (!AtEnd() && text_[index_] != ',' && text_[index_] != '}') {
        ++index_;
      }
      std::string value = text_.substr(start, index_ - start);
      while (!value.empty() && value.back() == ' ') {
        value.pop_back();
      }
      return value;
    }

  private:
    // 读取到下一个未转义的引号为止（不消费结尾引号）。
    std::string ReadQuoted() {
      std::string out;
      while (!AtEnd()) {
        const char ch = text_[index_];
        if (ch == '\\' && index_ + 1 < text_.size()) {
          out.push_back(ch);
          out.push_back(text_[index_ + 1]);
          index_ += 2;
          continue;
        }
        if (ch == '"') {
          ++index_;
          break;
        }
        out.push_back(ch);
        ++index_;
      }
      return out;
    }

    static std::string Unescape(const std::string &text) {
      std::string out;
      out.reserve(text.size());
      for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) {
          out.push_back(text[i]);
          continue;
        }
        const char next = text[++i];
        switch (next) {
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          default: out.push_back(next); break;
        }
      }
      return out;
    }

    const std::string &text_;
    std::size_t index_ = 0;
};

std::vector<Event> ParseLines(const std::string &path, bool *ok) {
  std::ifstream file(path, std::ios::binary);
  *ok = file.is_open();
  std::vector<Event> events;
  if (!*ok) {
    return events;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.size() < 2 || line.front() != '{') {
      continue;
    }
    Event event;
    event.raw = line;
    Scanner scanner(line);
    scanner.Skip('{');
    while (!scanner.AtEnd()) {
      const std::string key = scanner.ReadKey();
      scanner.Skip(':');
      const std::string value = scanner.ReadValue();
      event.fields[key] = value;
      scanner.Skip(',');
    }
    events.push_back(std::move(event));
  }
  return events;
}

std::map<std::string, std::string> ParseFilter(const char *text) {
  std::map<std::string, std::string> filter;
  const std::string item = text == nullptr ? std::string() : std::string(text);
  std::size_t start = 0;
  while (start <= item.size()) {
    const std::size_t comma = item.find(',', start);
    const std::string pair =
        item.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
    const std::size_t equals = pair.find('=');
    if (equals != std::string::npos) {
      filter[pair.substr(0, equals)] = pair.substr(equals + 1);
    }
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  return filter;
}

bool Matches(const Event &event, const std::map<std::string, std::string> &filter) {
  for (const auto &entry : filter) {
    const auto found = event.fields.find(entry.first);
    if (found == event.fields.end()) {
      return false;
    }
    if (found->second.find(entry.second) == std::string::npos) {
      return false;
    }
  }
  return true;
}

std::string Field(const Event &event, const char *key) {
  const auto found = event.fields.find(key);
  return found == event.fields.end() ? std::string() : found->second;
}

int Usage() {
  std::fprintf(stderr,
               "usage: krkr_debug --summary <events.jsonl>\n"
               "       krkr_debug --filter <key=value[,key=value]> <events.jsonl> [--raw]\n");
  return 2;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    return Usage();
  }

  const std::string mode = argv[1];
  // --filter 多一个参数（过滤条件），文件位置随之右移一格。
  const bool filtering = mode == "--filter";
  const int fileIndex = filtering ? 3 : 2;
  if (argc <= fileIndex) {
    return Usage();
  }

  bool ok = false;
  const std::vector<Event> events = ParseLines(argv[fileIndex], &ok);
  if (!ok) {
    std::fprintf(stderr, "cannot read %s\n", argv[fileIndex]);
    return 2;
  }

  if (mode == "--summary") {
    std::map<std::string, std::size_t> counts;
    for (const Event &event : events) {
      ++counts[Field(event, "ev")];
    }
    std::printf("events = %zu\n", events.size());
    for (const auto &entry : counts) {
      std::printf("  %-16s %zu\n", entry.first.c_str(), entry.second);
    }
    return 0;
  }

  if (filtering) {
    const auto filter = ParseFilter(argv[2]);
    const bool raw = argc > fileIndex + 1 && std::strcmp(argv[fileIndex + 1], "--raw") == 0;
    std::size_t matched = 0;
    for (const Event &event : events) {
      if (!Matches(event, filter)) {
        continue;
      }
      ++matched;
      if (raw) {
        std::printf("%s\n", event.raw.c_str());
        continue;
      }
      std::printf("[%s] %s", Field(event, "ev").c_str(), Field(event, "name").c_str());
      const std::string detail = Field(event, "detail");
      if (!detail.empty()) {
        std::printf(" :: %s", detail.c_str());
      }
      std::printf("\n");
    }
    std::printf("matched = %zu\n", matched);
    return matched > 0 ? 0 : 1;
  }

  return Usage();
}
