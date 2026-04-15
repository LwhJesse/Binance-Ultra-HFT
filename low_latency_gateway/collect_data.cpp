#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/ssl.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace std;

// --- 基础配置 ---
// 这里设极低(0.00005)，是为了采集所有微小波动，交给Python去筛选最佳阈值
const double DATA_COLLECT_THRESHOLD = 0.00005;
const long long WINDOW_US = 100000; // 100ms 窗口

struct SymbolState {
  double last_p = 0;
};
struct BtcSignal {
  double pct;       // 本次跳幅
  double prev_pct;  // 上次跳幅 (用于计算斜率/动量)
  long long ts;     // 信号时间
  long long lag;    // 网络延迟
  bool is_momentum; // 是否连续同向
  bool active = false;
};

SymbolState btc, sol;
BtcSignal sig;

// 1. 极速提取 (不产生临时字符串)
double fast_val(const char *buf, const char *key) {
  const char *p = strstr(buf, key);
  if (!p)
    return 0;
  p += strlen(key) + 3;
  return strtod(p, nullptr);
}

long long get_now() {
  return chrono::duration_cast<chrono::microseconds>(
             chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

void process(char *buf) {
  long long now = get_now();
  const char *s_pos = strstr(buf, "\"s\":\"");
  if (!s_pos)
    return;
  char sym_char = s_pos[5]; // B=BTC, S=SOL

  // 计算 NetLag
  const char *e_pos = strstr(buf, "\"E\":");
  long long lag = e_pos ? (now - (atoll(e_pos + 4) * 1000)) : 0;

  double p = fast_val(buf, "\"b\"");
  double q = fast_val(buf, "\"B\"");

  if (sym_char == 'B') { // BTC
    if (btc.last_p < 1.0) {
      btc.last_p = p;
      return;
    }
    double pct = (p - btc.last_p) / btc.last_p;

    if (abs(pct) >= DATA_COLLECT_THRESHOLD) {
      // 判定动量：如果本次和上次同向，则 momentum = true
      bool mom = (pct * sig.prev_pct > 0);

      sig = {pct, sig.prev_pct, now, lag, mom, true};
      sig.prev_pct = pct; // 更新历史

      // 屏幕只打印极简信息，防阻塞
      printf("Rec: %.5f%% | Mom: %d | Lag: %lld\n", pct * 100, mom, lag);
    }
    btc.last_p = p;
  } else if (sym_char == 'S') { // SOL (只抓 SOL，剔除 ORDI 杂音)
    if (!sig.active)
      return;
    long long delay = now - sig.ts;
    if (delay > WINDOW_US) {
      sig.active = false;
      return;
    }

    if (sol.last_p < 0.1) {
      sol.last_p = p;
      return;
    }
    double move = (p - sol.last_p) / sol.last_p;

    if (abs(move) > 0.00001) {
      // 核心落盘：Sym, BTC_Pct, SOL_Pct, Delay, NetLag, Is_Momentum, Qty
      static ofstream f("hft_data.csv", ios::app);
      f << "SOL," << sig.pct << "," << move << "," << delay << "," << lag << ","
        << sig.is_momentum << "," << q << endl;
      sol.last_p = p;
    }
  }
}

int main() {
  printf("HFT Data Miner v6.0 | Running...\n");
  SSL_library_init();
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

  while (true) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct hostent *h = gethostbyname("fstream.binance.com");
    struct sockaddr_in a;
    a.sin_family = AF_INET;
    a.sin_port = htons(443);
    memcpy(&a.sin_addr.s_addr, h->h_addr, h->h_length);

    // 开启 TCP_NODELAY 禁用 Nagle 算法 (极重要)
    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

    if (connect(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
      sleep(1);
      continue;
    }
    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, s);
    SSL_set_tlsext_host_name(ssl, "fstream.binance.com");
    SSL_connect(ssl);

    // 只订阅 BTC 和 SOL，减少带宽干扰
    string req =
        "GET /stream?streams=btcusdt@bookTicker/solusdt@bookTicker "
        "HTTP/1.1\r\nHost: fstream.binance.com\r\nUpgrade: "
        "websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
        "dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    SSL_write(ssl, req.c_str(), req.length());

    char buf[65536];
    int rem = 0;
    while (true) {
      int len = SSL_read(ssl, buf + rem, sizeof(buf) - rem);
      if (len <= 0)
        break;
      int tot = len + rem;
      char *p = buf;
      char *e = p + tot;
      while (p < e) {
        if ((unsigned char)p[0] != 0x81) {
          p++;
          continue;
        }
        if (e - p < 2)
          break;
        size_t hl = 2;
        uint64_t pl = p[1] & 127;
        if (pl == 126) {
          hl = 4;
          pl = ((unsigned char)p[2] << 8) | (unsigned char)p[3];
        } else if (pl == 127) {
          hl = 10;
          pl = 0;
          for (int i = 0; i < 8; i++)
            pl = (pl << 8) | (unsigned char)p[2 + i];
        }
        if (e - p < hl + pl)
          break;
        p[hl + pl] = '\0';
        process(p + hl);
        p += hl + pl;
      }
      rem = e - p;
      if (rem > 0)
        memmove(buf, p, rem);
      else
        rem = 0;
    }
    SSL_free(ssl);
    close(s);
  }
}
