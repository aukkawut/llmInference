#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define BUFFER_SIZE 10240
#define MAX_MESSAGES 100

struct memory {
    char *response;
    size_t size;
};

typedef struct {
    char role[16];     // "user" or "assistant"
    char *content;
} Message;

Message history[MAX_MESSAGES];
int history_size = 0;

const char *openai_api_key = NULL;
const char *anthropic_api_key = NULL;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if(ptr == NULL) {
        fprintf(stderr, "realloc returned NULL.\n");
        return 0;
    }
    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->response[mem->size] = 0;

    return realsize;
}
void add_message(const char *role, const char *content) {
    if(history_size >= MAX_MESSAGES) {
        free(history[0].content);
        memmove(&history[0], &history[1], (MAX_MESSAGES - 1) * sizeof(Message));
        history_size--;
    }
    strcpy(history[history_size].role, role);
    history[history_size].content = strdup(content);
    history_size++;
}

cJSON *build_messages_json() {
    cJSON *messages = cJSON_CreateArray();
    for(int i = 0; i < history_size; i++) {
        cJSON *msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", history[i].role);
        cJSON_AddStringToObject(msg, "content", history[i].content);
        cJSON_AddItemToArray(messages, msg);
    }
    return messages;
}
void list_available_models() {
    if (openai_api_key) {
        CURL *curl = curl_easy_init();
        if(!curl) return;
        struct memory chunk = {0};
        struct curl_slist *headers = NULL;
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", openai_api_key);
        headers = curl_slist_append(headers, auth_header);
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/models");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        printf("Fetching OpenAI models...\n");
        CURLcode res = curl_easy_perform(curl);
        if(res == CURLE_OK) {
            cJSON *json = cJSON_Parse(chunk.response);
            if(json) {
                cJSON *data = cJSON_GetObjectItem(json, "data");
                if(cJSON_IsArray(data)) {
                    printf("--- OpenAI Models ---\n");
                    cJSON *model;
                    cJSON_ArrayForEach(model, data) {
                        cJSON *id = cJSON_GetObjectItem(model, "id");
                        if(cJSON_IsString(id) && strstr(id->valuestring, "gpt")) {
                            printf("- %s\n", id->valuestring);
                        }
                    }
                }
                cJSON_Delete(json);
            }
        }
        free(chunk.response);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
    if (anthropic_api_key) {
        CURL *curl = curl_easy_init();
        if(!curl) return;   
        struct memory chunk = {0};
        struct curl_slist *headers = NULL;
        char api_key_header[256];
        snprintf(api_key_header, sizeof(api_key_header), "x-api-key: %s", anthropic_api_key);
        headers = curl_slist_append(headers, api_key_header);
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/models");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        printf("Fetching Anthropic models...\n");
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            cJSON *json = cJSON_Parse(chunk.response);
            if(json) {
                cJSON *data = cJSON_GetObjectItem(json, "data");
                if(cJSON_IsArray(data)) {
                    printf("--- Anthropic Claude Models ---\n");
                    cJSON *model;
                    cJSON_ArrayForEach(model, data) {
                        cJSON *id = cJSON_GetObjectItem(model, "id");
                        if(cJSON_IsString(id)) {
                            printf("- %s\n", id->valuestring);
                        }
                    }
                }
                cJSON_Delete(json);
            }
        }
        free(chunk.response);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

void chat_with_openai(const char *model, const char *message) {
    if (!openai_api_key) {
        fprintf(stderr, "missing OPENAI_API_KEY\n");
        return;
    }
    CURL *curl = curl_easy_init();
    if(!curl) return;
    struct memory chunk = {0};
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", openai_api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON *messages_json = build_messages_json();
    cJSON_AddItemToObject(root, "messages", messages_json);
    char *postdata = cJSON_PrintUnformatted(root);
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.openai.com/v1/chat/completions");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        cJSON *json = cJSON_Parse(chunk.response);
        if(json) {
            cJSON *choices = cJSON_GetObjectItem(json, "choices");
            if(cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
                cJSON *message_obj = cJSON_GetObjectItem(first_choice, "message");
                if(message_obj) {
                    cJSON *content = cJSON_GetObjectItem(message_obj, "content");
                    if(cJSON_IsString(content)) {
                        printf("AI: %s\n", content->valuestring);
                        add_message("assistant", content->valuestring);
                    }
                }
            } else {
                 cJSON *error = cJSON_GetObjectItem(json, "error");
                 if (error) {
                     cJSON *error_message = cJSON_GetObjectItem(error, "message");
                     if (cJSON_IsString(error_message)) {
                         fprintf(stderr, "API Error: %s\n", error_message->valuestring);
                     }
                 } else {
                    printf("Well, something surely happens...\n");
                 }
            }
            cJSON_Delete(json);
        } else {
            fprintf(stderr, "damn JSON\n");
        }
    }
    free(postdata);
    free(chunk.response);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    cJSON_Delete(root);
}
void chat_with_claude(const char *model, const char *message) {
    if (!anthropic_api_key) {
        fprintf(stderr, "missing ANTHROPIC_API_KEY\n");
        return;
    }
    CURL *curl = curl_easy_init();
    if(!curl) return;

    struct memory chunk = {0};
    struct curl_slist *headers = NULL;
    char api_key_header[256];
    snprintf(api_key_header, sizeof(api_key_header), "x-api-key: %s", anthropic_api_key);
    headers = curl_slist_append(headers, api_key_header);
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddNumberToObject(root, "max_tokens", 4096);
    cJSON *messages_json = build_messages_json();
    cJSON_AddItemToObject(root, "messages", messages_json);
    char *postdata = cJSON_PrintUnformatted(root);
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postdata);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
        cJSON *json = cJSON_Parse(chunk.response);
        if(json) {
            cJSON *content_array = cJSON_GetObjectItem(json, "content");
            if(cJSON_IsArray(content_array) && cJSON_GetArraySize(content_array) > 0) {
                cJSON *first_content = cJSON_GetArrayItem(content_array, 0);
                cJSON *text = cJSON_GetObjectItem(first_content, "text");
                if(cJSON_IsString(text)) {
                    printf("AI: %s\n", text->valuestring);
                    add_message("assistant", text->valuestring);
                }
            } else { // Handle API errors
                 cJSON *error = cJSON_GetObjectItem(json, "error");
                 if (error) {
                     cJSON *error_message = cJSON_GetObjectItem(error, "message");
                     if (cJSON_IsString(error_message)) {
                         fprintf(stderr, "API Error: %s\n", error_message->valuestring);
                     }
                 } else {
                    printf("Something surely happens...\n");
                 }
            }
            cJSON_Delete(json);
        } else {
            fprintf(stderr, "damn JSON\n");
        }
    }
    free(postdata);
    free(chunk.response);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    cJSON_Delete(root);
}
void chat_message(const char *model, const char *message) {
    add_message("user", message);
    if (strstr(model, "claude") != NULL) {
        chat_with_claude(model, message);
    } else {
        chat_with_openai(model, message);
    }
}

int main() {
    openai_api_key = getenv("OPENAI_API_KEY");
    anthropic_api_key = getenv("ANTHROPIC_API_KEY");
    if(!openai_api_key && !anthropic_api_key) {
        fprintf(stderr, "Where are your API keys?\n");
        return 1;
    }

    char input[2048];
    char model[128] = "chatgpt-4o-latest"; // Default model
    printf("Commands: /model to change model, /quit to exit\n");
    printf("Current Model: %s\n", model);

    while(1) {
        printf("> ");
        if(!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = 0;
        if(strlen(input) == 0) continue; 
        if(strcmp(input, "/quit") == 0) break;
        if(strcmp(input, "/model") == 0) {
            list_available_models();
            printf("Enter model name to use: ");
            if(fgets(model, sizeof(model), stdin)) {
                model[strcspn(model, "\n")] = 0;
                printf("Model set to: %s\n", model);
            }
            continue;
        }
        chat_message(model, input);
    }
    for(int i = 0; i < history_size; i++) {
        if(history[i].content) free(history[i].content);
    }
    return 0;
}
