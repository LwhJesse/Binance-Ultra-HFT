#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// Helper: Print OpenSSL errors
void print_ssl_error() { ERR_print_errors_fp(stderr); }

int main() {
  // ------------------------------------------------------------------
  // 1. Initialization
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 1. Initializing OpenSSL...\n");
  SSL_library_init();
  SSL_load_error_strings();
  SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());

  // ------------------------------------------------------------------
  // 2. DNS Resolution
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 2. Resolving fstream.binance.com...\n");
  struct hostent *host = gethostbyname("fstream.binance.com");
  if (!host) {
    perror("[FATAL] DNS Resolution Error");
    return 1;
  }

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(443);
  memcpy(&addr.sin_addr.s_addr, host->h_addr, host->h_length);

  // ------------------------------------------------------------------
  // 3. TCP Connection & Low Latency Tuning
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 3. Establishing TCP connection...\n");
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  int flag = 1;
  // Apply TCP_NODELAY for extreme low latency
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));

  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("[FATAL] TCP Connect Error");
    return 1;
  }

  // ------------------------------------------------------------------
  // 4. TLS Handshake (with SNI handling)
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 4. Performing TLS Handshake...\n");
  SSL *ssl = SSL_new(ctx);
  SSL_set_fd(ssl, sock);

  // SNI setup - Crucial for avoiding handshake rejections behind Cloudflare/AWS
  if (!SSL_set_tlsext_host_name(ssl, "fstream.binance.com")) {
    fprintf(stderr, "[FATAL] SNI Setup Failed\n");
    return 1;
  }

  int ret = SSL_connect(ssl);
  if (ret != 1) {
    fprintf(stderr, "[ERROR] TLS Handshake failed! Return code: %d\n", ret);
    print_ssl_error();
    return 1;
  }
  fprintf(stderr, "[SUCCESS] TLS Handshake complete!\n");

  // ------------------------------------------------------------------
  // 5. Send WebSocket Upgrade Request
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 5. Sending Protocol Upgrade request...\n");

  char req[] = "GET /ws/btcusdt@depth HTTP/1.1\r\n"
               "Host: fstream.binance.com\r\n"
               "Upgrade: websocket\r\n"
               "Connection: Upgrade\r\n"
               "User-Agent: C-Core/Low-Latency-Gateway\r\n"
               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n";

  if (SSL_write(ssl, req, strlen(req)) <= 0) {
    fprintf(stderr, "[FATAL] Socket Write Error\n");
    print_ssl_error();
    return 1;
  }

  // ------------------------------------------------------------------
  // 6. Receive Loop (Raw Payload Extraction)
  // ------------------------------------------------------------------
  fprintf(stderr, "[DEBUG] 6. Entering receive loop...\n");

  char buf[8192];
  int first_packet = 1;

  while (1) {
    int len = SSL_read(ssl, buf, sizeof(buf));

    if (len <= 0) {
      int err = SSL_get_error(ssl, len);
      fprintf(stderr, "\n[ERROR] Connection closed. Length: %d, SSL Status: %d\n", len, err);
      break;
    }

    if (first_packet) {
      fprintf(stderr, "[DEBUG] Received server response header (%d bytes):\n", len);
      fwrite(buf, 1, len, stderr);
      fprintf(stderr, "\n--------------------------------\n");

      if (strstr(buf, "101 Switching Protocols") == NULL) {
        fprintf(stderr, "[FATAL] Server rejected the upgrade request! Check headers above.\n");
      } else {
        fprintf(stderr, "[SUCCESS] WebSocket upgraded successfully! Streaming data to stdout...\n");
      }
      first_packet = 0;
    }

    // Parse WebSocket frame header to locate payload
    unsigned char *ptr = (unsigned char *)buf;
    int header_len = 2;
    int payload_len = ptr[1] & 127;

    if (payload_len == 126) header_len = 4;
    if (payload_len == 127) header_len = 10;

    // Filter HTTP response header, output only JSON payload to stdout (for piping)
    if (buf[0] != 'H' || buf[1] != 'T') {
      if (len > header_len) {
        write(1, buf + header_len, len - header_len);
        write(1, "\n", 1);
      }
    }
  }
  return 0;
}
