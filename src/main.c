#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define nullptr (void*)0

size_t write_callback(void *data, size_t size, size_t nmemb, char **userp) {
  size_t total_size = size * nmemb;
  *userp = malloc(total_size + 1);
  strncat(*userp, data, total_size);
  return total_size;
}

json_object *ollama_new_message(const char *role, const char *content) {
  json_object *obj = json_object_new_object();

  json_object_object_add(obj, "role", json_object_new_string(role));
  json_object_object_add(obj, "content", json_object_new_string(content));

  return obj;
}

typedef struct {
  char *ptr;
  size_t total_size;
} string_dynamic_t;

string_dynamic_t string_dynamic_new(const char *str) {
  if (str == nullptr) return (string_dynamic_t){nullptr, 0};
  size_t size = strlen(str);
  char *new_ptr = malloc(size + 1);
  memcpy(new_ptr, str, size);
  return (string_dynamic_t){.ptr = new_ptr, .total_size = size};
}

void string_dynamic_append(string_dynamic_t *self, const char *str, const size_t *opt_size) {
  size_t size;
  if (opt_size != nullptr) {
    size = *opt_size;
  } else {
    size = strlen(str);
  }
  const size_t total_size = self->total_size + size;
  char *reallocated = realloc(self->ptr, total_size);
  if (reallocated == nullptr) return;
  self->ptr = reallocated;
  memcpy(self->ptr + self->total_size, str, size);
  self->total_size = total_size;
  self->total_size = total_size;
}

/// nullptr if error, otherwise a malloc allocated ptr is returned
char *get_git_diff() {
  char *buffer = nullptr;
  size_t buffer_size = 0;
  size_t n;
  const char *command = "git diff --staged";
  FILE *fp = popen(command, "r");

  string_dynamic_t string_dynamic = string_dynamic_new(nullptr);


  if (fp == NULL) {
    perror("Can't run `git diff --staged`");
    return nullptr;
  }

  while ((n = getline(&buffer, &buffer_size, fp)) != -1) {
    string_dynamic_append(&string_dynamic, buffer, &n);
  }


  if (n == -1 && !feof(fp)) {
    perror("Can't read from `git diff --staged`");
    free(buffer);
    free(string_dynamic.ptr);
    fclose(fp);
    return nullptr;
  }

  free(buffer);
  pclose(fp);
  return string_dynamic.ptr;
}

json_object *create_new_request(char *diff) {
  char *model = "qwen2.5-coder:7b";
  char *base_prompt =
      "Generate a commit message for the above set of changes. First, give a single sentence, no more than 80 characters. Then, after 2 line breaks, give a list of no more than 5 short bullet points, each no more than 40 characters. Output nothing except for the commit message, and don't surround it in quotes.";
  char *prompt;
  asprintf(&prompt, "```%s```\n\n%s", diff, base_prompt);

  json_object *obj = json_object_new_object();
  json_object_object_add(obj, "model", json_object_new_string(model));
  json_object_object_add(obj, "stream", json_object_new_boolean(false));
  json_object *messages = json_object_new_array_ext(1);
  json_object *message = ollama_new_message("user", prompt);
  json_object_array_add(messages, message);
  json_object_object_add(obj, "messages", messages);
  return obj;
}

const char *content_result_of_json_response(const json_object *response) {
  const json_object *response_message_obj = json_object_object_get(response, "message");
  json_object *response_message_content_obj = json_object_object_get(response_message_obj, "content");
  const char *content = json_object_get_string(response_message_content_obj);
  return content;
}

typedef struct {
  char *protocol;
  char *host;
  char *port;
} ollama_host_t;

ollama_host_t get_host() {
  CURLU *h = curl_url();

  char *host = getenv("OLLAMA_HOST");
  size_t host_len = host ? strlen(host) : 0;
  if (host == nullptr || host_len == 0) {
    return (ollama_host_t){.protocol = nullptr, .host = nullptr, .port = nullptr};
  }

  CURLUcode rc = curl_url_set(h, CURLUPART_URL, host, 0);

  if (rc == CURLUE_BAD_SCHEME) {
    char *new_host;
    asprintf(&new_host, "http://%s", host);
    rc = curl_url_set(h, CURLUPART_URL, new_host, 0);
    free(new_host);
  }

  if (rc != CURLE_OK) {
    return (ollama_host_t){.protocol = nullptr, .host = nullptr, .port = nullptr};
  }

  ollama_host_t basic_host = {.protocol = nullptr, .host = nullptr, .port = nullptr};

  char *scheme;
  const CURLUcode rc_scheme = curl_url_get(h, CURLUPART_SCHEME, &scheme, 0);
  if (rc_scheme == CURLE_OK) {
    basic_host.protocol = scheme;
  }

  char *host_part;
  const CURLUcode rc_host = curl_url_get(h, CURLUPART_HOST, &host_part, 0);
  if (rc_host == CURLE_OK) {
    basic_host.host = host_part;
  }

  char *port;
  const CURLUcode rc_port = curl_url_get(h, CURLUPART_PORT, &port, 0);
  if (rc_port == CURLE_OK) {
    basic_host.port = port;
  }

  curl_url_cleanup(h);
  return basic_host;
}

void ollama_host_free(const ollama_host_t *host) {
  if (host == nullptr) return;
  if (host->protocol != nullptr) curl_free(host->protocol);
  if (host->host != nullptr) curl_free(host->host);
  if (host->port != nullptr) curl_free(host->port);
}

int main(void) {
  const ollama_host_t host = get_host();
  string_dynamic_t url = string_dynamic_new(nullptr);
  if (host.protocol != nullptr) {
    string_dynamic_append(&url, host.protocol, nullptr);
  } else {
    string_dynamic_append(&url, "http", nullptr);
  }
  string_dynamic_append(&url, "://", nullptr);
  if (host.host != nullptr) {
    string_dynamic_append(&url, host.host, nullptr);
  } else {
    string_dynamic_append(&url, "127.0.0.1", nullptr);
  }
  string_dynamic_append(&url, ":", nullptr);
  if (host.port != nullptr) {
    string_dynamic_append(&url, host.port, nullptr);
  } else {
    string_dynamic_append(&url, "11434", nullptr);
  }
  string_dynamic_append(&url, "/api/chat", nullptr);
  ollama_host_free(&host);

  char *diff = get_git_diff();
  if (diff == nullptr) {
    perror("Fatal error has occurred");
    return EXIT_FAILURE;
  }
  json_object *request = create_new_request(diff);
  free(diff);
  const char *request_str = json_object_to_json_string(request);

  char *response[] = {nullptr};
  curl_global_init(CURL_GLOBAL_ALL);
  CURL *curl = curl_easy_init();

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.ptr);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    const CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      fprintf(stderr, "POST request failed: %s\n", curl_easy_strerror(res));
    } else {
      json_object *response_obj = json_tokener_parse(*response);
      const char *content_response = content_result_of_json_response(response_obj);
      printf("%s\n", content_response);
      json_object_put(response_obj);
    }

    curl_easy_cleanup(curl);
    free(*response);
  }

  free(url.ptr);
  json_object_put(request);
  curl_global_cleanup();

  return 0;
}
