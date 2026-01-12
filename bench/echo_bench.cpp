#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static volatile bool g_stop = false;
void handle_sigint(int) { g_stop = true; }

struct Conn {
  int fd{-1};
  bool connected{false};
  std::string msg;
  size_t write_off{0};
  size_t read_remaining{0};
  Clock::time_point t0;
  uint64_t completed{0};
};

static int set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return -1;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int connect_nonblock(const std::string& host, int port) {
  struct addrinfo hints{};
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = nullptr;
  char portstr[16];
  snprintf(portstr, sizeof(portstr), "%d", port);
  int rc = getaddrinfo(host.c_str(), portstr, &hints, &res);
  if (rc != 0) return -1;
  int fd = -1;
  for (auto p = res; p; p = p->ai_next) {
    fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd < 0) continue;
    set_nonblock(fd);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    rc = ::connect(fd, p->ai_addr, p->ai_addrlen);
    if (rc == 0 || errno == EINPROGRESS) {
      freeaddrinfo(res);
      return fd;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return -1;
}

struct Options {
  std::string host{"127.0.0.1"};
  int port{8888};
  int connections{200};
  int duration_sec{20};
  std::string message{"ping\n"};
  std::string log_file{"echo_bench_result.log"};  // 日志文件
};

static void usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [--host 127.0.0.1] [--port 8888] [--connections 200] "
               "[--duration 20] [--message \"ping\\n\"] [--log result.log]\n";
}

int main(int argc, char** argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto need = [&](int& out) {
      if (i + 1 >= argc) {
        usage(argv[0]);
        exit(1);
      }
      out = atoi(argv[++i]);
      return 0;
    };
    auto needs = [&](std::string& out) {
      if (i + 1 >= argc) {
        usage(argv[0]);
        exit(1);
      }
      out = argv[++i];
      return 0;
    };
    if (a == "--host")
      needs(opt.host);
    else if (a == "--port")
      need(opt.port);
    else if (a == "--connections")
      need(opt.connections);
    else if (a == "--duration")
      need(opt.duration_sec);
    else if (a == "--message")
      needs(opt.message);
    else if (a == "--log")
      needs(opt.log_file);
    else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      usage(argv[0]);
      return 1;
    }
  }

  signal(SIGINT, handle_sigint);

  int ep = epoll_create1(EPOLL_CLOEXEC);
  if (ep < 0) {
    perror("epoll_create1");
    return 1;
  }

  std::vector<Conn> conns(opt.connections);
  for (int i = 0; i < opt.connections; ++i) {
    int fd = connect_nonblock(opt.host, opt.port);
    if (fd < 0) {
      std::cerr << "connect failed at index " << i << "\n";
      return 1;
    }
    Conn& c = conns[i];
    c.fd = fd;
    c.connected = false;
    c.msg = opt.message;
    c.write_off = 0;
    c.read_remaining = 0;
    c.completed = 0;
    struct epoll_event ev{};
    ev.data.u32 = i;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) < 0) {
      perror("epoll_ctl add");
      return 1;
    }
  }

  auto start = Clock::now();
  auto deadline = start + std::chrono::seconds(opt.duration_sec);
  std::vector<double> latencies_ms;
  latencies_ms.reserve(100000);

  std::vector<char> rbuf(1 << 16);
  const int MAX_EVENTS = 1024;
  std::vector<epoll_event> events(MAX_EVENTS);

  auto now = [&]() { return Clock::now(); };
  auto ms = [&](Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
  };

  while (!g_stop && now() < deadline) {
    int n = epoll_wait(ep, events.data(), MAX_EVENTS, 100);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      else {
        perror("epoll_wait");
        break;
      }
    }
    for (int i = 0; i < n; ++i) {
      uint32_t idx = events[i].data.u32;
      if (idx >= conns.size()) continue;
      Conn& c = conns[idx];
      if (c.fd < 0) continue;
      uint32_t ev = events[i].events;

      if (!c.connected) {
        int err = 0;
        socklen_t len = sizeof(err);
        if (getsockopt(c.fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 &&
            err == 0) {
          c.connected = true;
          c.write_off = 0;
          c.read_remaining = 0;
        }
      }

      if (ev & (EPOLLOUT)) {
        // If nothing in flight, start a new request
        if (c.read_remaining == 0 && c.write_off == 0) {
          c.t0 = now();
        }
        while (c.write_off < c.msg.size()) {
          ssize_t w = ::send(c.fd, c.msg.data() + c.write_off,
                             c.msg.size() - c.write_off, 0);
          if (w > 0) {
            c.write_off += (size_t)w;
          } else {
            if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            // error
            close(c.fd);
            c.fd = -1;
            break;
          }
        }
        if (c.fd >= 0 && c.write_off == c.msg.size()) {
          // Write complete, expect echo of same size
          c.read_remaining = c.msg.size();
          c.write_off = 0;
        }
      }

      if (c.fd >= 0 && (ev & (EPOLLIN))) {
        while (c.read_remaining > 0) {
          ssize_t r =
              ::recv(c.fd, rbuf.data(),
                     std::min<size_t>(rbuf.size(), c.read_remaining), 0);
          if (r > 0) {
            c.read_remaining -= (size_t)r;
            if (c.read_remaining == 0) {
              // one RTT completed
              double l = ms(c.t0, now());
              latencies_ms.push_back(l);
              c.completed++;
              // Trigger next send immediately via EPOLLOUT readiness on next
              // loop
            }
          } else {
            if (r == 0) {
              close(c.fd);
              c.fd = -1;
              break;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close(c.fd);
            c.fd = -1;
            break;
          }
        }
      }

      if (ev & (EPOLLERR | EPOLLHUP)) {
        close(c.fd);
        c.fd = -1;
      }
    }
  }

  // Close all
  for (auto& c : conns)
    if (c.fd >= 0) close(c.fd);

  // Aggregate stats
  uint64_t total_req = 0;
  uint64_t failed_conns = 0;
  for (auto& c : conns) {
    total_req += c.completed;
    if (c.completed == 0) failed_conns++;
  }
  double test_sec = std::chrono::duration<double>(now() - start).count();
  double rps = total_req / std::max(1e-9, test_sec);

  std::sort(latencies_ms.begin(), latencies_ms.end());
  auto pct = [&](double p) {
    if (latencies_ms.empty()) return 0.0;
    size_t i = (size_t)(p * (latencies_ms.size() - 1));
    return latencies_ms[i];
  };

  // 计算额外的统计信息
  double min_latency = latencies_ms.empty() ? 0.0 : latencies_ms.front();
  double max_latency = latencies_ms.empty() ? 0.0 : latencies_ms.back();
  double avg_latency = 0.0;
  if (!latencies_ms.empty()) {
    for (auto l : latencies_ms) avg_latency += l;
    avg_latency /= latencies_ms.size();
  }

  double success_rate =
      (double)(opt.connections - failed_conns) / opt.connections * 100.0;

  // 构建输出字符串
  std::ostringstream oss;
  oss << "\n";
  oss << "================================================\n";
  oss << "         Echo Bench Test Report\n";
  oss << "================================================\n";
  oss << "Test Configuration:\n";
  oss << "  Server: " << opt.host << ":" << opt.port << "\n";
  oss << "  Connections: " << opt.connections << "\n";
  oss << "  Duration: " << opt.duration_sec << " seconds\n";
  oss << "  Message size: " << opt.message.size() << " bytes\n";
  oss << "\n";
  oss << "Test Results:\n";
  oss << "  Actual duration: " << std::fixed << std::setprecision(2) << test_sec
      << " seconds\n";
  oss << "  Total requests: " << total_req << "\n";
  oss << "  Successful connections: " << (opt.connections - failed_conns)
      << " / " << opt.connections << "\n";
  oss << "  Connection success rate: " << std::fixed << std::setprecision(2)
      << success_rate << "%\n";
  oss << "\n";
  oss << "Performance Metrics:\n";
  oss << "  Throughput: " << std::fixed << std::setprecision(2) << rps
      << " req/s\n";
  oss << "  Min latency: " << std::fixed << std::setprecision(4) << min_latency
      << " ms\n";
  oss << "  Max latency: " << std::fixed << std::setprecision(4) << max_latency
      << " ms\n";
  oss << "  Avg latency: " << std::fixed << std::setprecision(4) << avg_latency
      << " ms\n";
  oss << "\n";
  oss << "Latency Percentiles:\n";
  oss << "  p50: " << std::fixed << std::setprecision(4) << pct(0.50)
      << " ms\n";
  oss << "  p75: " << std::fixed << std::setprecision(4) << pct(0.75)
      << " ms\n";
  oss << "  p90: " << std::fixed << std::setprecision(4) << pct(0.90)
      << " ms\n";
  oss << "  p95: " << std::fixed << std::setprecision(4) << pct(0.95)
      << " ms\n";
  oss << "  p99: " << std::fixed << std::setprecision(4) << pct(0.99)
      << " ms\n";
  oss << "  p99.9: " << std::fixed << std::setprecision(4) << pct(0.999)
      << " ms\n";
  oss << "================================================\n";

  std::string report = oss.str();

  // 输出到控制台
  std::cout << report;

  // 输出到日志文件
  std::ofstream log_file(opt.log_file, std::ios::app);
  if (log_file.is_open()) {
    log_file << "Test at " << std::time(nullptr) << "\n" << report << "\n";
    log_file.close();
    std::cout << "Results saved to: " << opt.log_file << "\n";
  }

  return 0;
}
