#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// 辅助函数：打印 OpenSSL 错误
void print_ssl_error() { ERR_print_errors_fp(stderr); }

int main() {
  // ------------------------------------------------------------------
  // 1. 初始化
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 1. 初始化 OpenSSL...\n");
  SSL_library_init();
  SSL_load_error_strings();
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

  // ------------------------------------------------------------------
  // 2. DNS 解析
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 2. 解析域名 fstream.binance.com...\n");
  struct hostent *host = gethostbyname("fstream.binance.com");
  if (!host) {
    perror("DNS Error");
    return 1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(443);
  memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);

  // ------------------------------------------------------------------
  // 3. 物理连接 & 极速设置
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 3. 建立 TCP 连接...\n");
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  int flag = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag,
             sizeof(int)); // 物理极速

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Connect Error");
    return 1;
  }

  // ------------------------------------------------------------------
  // 4. SSL 握手 (带 SNI 修复)
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 4. 执行 TLS 握手 (关键步骤)...\n");
  SSL *ssl = SSL_new(ctx);
  SSL_set_fd(ssl, sock);

  // 【死罪修复】SNI 设置
  if (!SSL_set_tlsext_host_name(ssl, "fstream.binance.com")) {
    fprintf(stderr, "SNI Set Failed\n");
    return 1;
  }

  int ret = SSL_connect(ssl);
  if (ret != 1) {
    fprintf(stderr, "❌ SSL 握手失败! 返回值: %d\n", ret);
    print_ssl_error();
    return 1;
  }
  fprintf(stderr, "✅ TLS 握手成功！\n");

  // ------------------------------------------------------------------
  // 5. 发送 WebSocket 升级请求
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 5. 发送 Protocol Upgrade 请求...\n");

  char req[] = "GET /ws/btcusdt@depth HTTP/1.1\r\n"
               "Host: fstream.binance.com\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "User-Agent: Python/3.9 websocket-client/1.6.1\r\n"
               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n";

  if (SSL_write(ssl, req, strlen(req)) <= 0) {
    fprintf(stderr, "Write Error\n");
    print_ssl_error();
    return 1;
  }

  // ------------------------------------------------------------------
  // 6. 接收循环 (无过滤，全裸输出)
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 6. 进入接收循环...\n");

  char buf[8192];
  int first_packet = 1;

  while (1) {
    int len = SSL_read(ssl, buf, sizeof(buf));

    if (len <= 0) {
      int err = SSL_get_error(ssl, len);
      fprintf(stderr, "\n❌ 连接结束. 错误码: %d, SSL状态: %d\n", len, err);
      break;
    }

    // 如果是第一个包，打印响应头用于调试
    if (first_packet) {
      fprintf(stderr, "[DEBUG] 收到服务器响应头 (%d bytes):\n", len);
      // 打印部分响应头防止刷屏，但保留关键信息
      fwrite(buf, 1, len, stderr);
      fprintf(stderr, "\n--------------------------------\n");

      if (strstr(buf, "101 Switching Protocols") == NULL) {
        fprintf(stderr,
                "❌ 致命错误：服务器拒绝了升级请求！请看上面的响应头。\n");
      } else {
        fprintf(stderr, "✅ WebSocket 升级成功！正在流式传输...\n");
      }
      first_packet = 0;
    }

    // 解析 WebSocket 帧头，定位 Payload
    unsigned char *ptr = (unsigned char *)buf;
    int header_len = 2;
    int payload_len = ptr[1] & 127;

    if (payload_len == 126)
      header_len = 4;
    if (payload_len == 127)
      header_len = 10;

    // 过滤 HTTP 响应包，只输出 JSON 数据
    if (buf[0] != 'H' || buf[1] != 'T') {
      if (len > header_len) {
        // 写到 stdout (进入管道)
        write(1, buf + header_len, len - header_len);
        write(1, "\n", 1);
      }
    }
  }
  return 0;
}
