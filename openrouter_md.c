#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define BUFFER_SIZE 10240
#define MAX_MESSAGES 100
#define MAX_SELECTABLE_MODELS 500

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
const char *openrouter_api_key = NULL;
char* selectable_models[MAX_SELECTABLE_MODELS];
int num_selectable_models = 0;
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct memory *mem = (struct memory *)userp;

    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if(ptr == NULL) {
        fprintf(stderr, "realloc() failed\n");
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

// Fetch free models along with openai and anthropic
void list_available_models() {
    for(int i = 0; i < num_selectable_models; i++) {
        free(selectable_models[i]);
        selectable_models[i] = NULL;
    }
    num_selectable_models = 0;

    CURL *curl = curl_easy_init();
    if(!curl) return;

    struct memory chunk = { .response = malloc(1), .size = 0 };

    curl_easy_setopt(curl, CURLOPT_URL, "https://openrouter.ai/api/v1/models");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

    printf("Fetching OpenRouter models...\n");
    CURLcode res = curl_easy_perform(curl);

    if(res == CURLE_OK) {
        cJSON *json = cJSON_Parse(chunk.response);
        if(json) {
            cJSON *data = cJSON_GetObjectItem(json, "data");
            if(cJSON_IsArray(data)) {
                printf("--- Available Models (Free, OpenAI, Anthropic) ---\n");
                cJSON *model;
                cJSON_ArrayForEach(model, data) {
                    cJSON *id = cJSON_GetObjectItem(model, "id");
                    if(cJSON_IsString(id)) {
                        const char *model_id = id->valuestring;
                        size_t id_len = strlen(model_id);

                        const char* suffix = ":free";
                        size_t suffix_len = strlen(suffix);
                        int ends_with_free = (id_len >= suffix_len && strcmp(model_id + id_len - suffix_len, suffix) == 0);

                        const char* openai_prefix = "openai/";
                        int starts_with_openai = (strncmp(model_id, openai_prefix, strlen(openai_prefix)) == 0);

                        const char* anthropic_prefix = "anthropic/";
                        int starts_with_anthropic = (strncmp(model_id, anthropic_prefix, strlen(anthropic_prefix)) == 0);

                        if (ends_with_free || starts_with_openai || starts_with_anthropic) {
                             if (num_selectable_models < MAX_SELECTABLE_MODELS) {
                                printf("[%d] %s\n", num_selectable_models + 1, model_id);
                                selectable_models[num_selectable_models] = strdup(model_id);
                                num_selectable_models++;
                            }
                        }
                    }
                }
            }
            cJSON_Delete(json);
        } else {
            fprintf(stderr, "Failed to parse JSON for model list.\n");
        }
    } else {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    }

    free(chunk.response);
    curl_easy_cleanup(curl);
}

void chat_with_openrouter(const char *model, const char *message) {
    if (!openrouter_api_key) {
        fprintf(stderr, "Where is your API key\n");
        return;
    }
    CURL *curl = curl_easy_init();
    if(!curl) return;
    struct memory chunk = { .response = malloc(1), .size = 0 };
    struct curl_slist *headers = NULL;
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", openrouter_api_key);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON_AddItemToObject(root, "messages", build_messages_json());
    cJSON *reasoning = cJSON_CreateObject();
    cJSON_AddBoolToObject(reasoning, "exclude", true);
    cJSON_AddItemToObject(root, "reasoning", reasoning);
    char *postdata = cJSON_PrintUnformatted(root);
    curl_easy_setopt(curl, CURLOPT_URL, "https://openrouter.ai/api/v1/chat/completions");
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
                cJSON *message_obj = cJSON_GetObjectItem(cJSON_GetArrayItem(choices, 0), "message");
                if(message_obj) {
                    cJSON *content = cJSON_GetObjectItem(message_obj, "content");
                    if(cJSON_IsString(content)) {
                        // Render assistant message as Markdown using txc + glow
			const char *ai_output = content->valuestring;
			add_message("assistant", ai_output);

			// Write markdown to temporary file
			FILE *mdfile = fopen("markdown.md", "w");
			if (mdfile) {
    				fputs(ai_output, mdfile);
    				fclose(mdfile);

    				// Render markdown using txc and glow
    				int render_status = system("txc -f markdown.md -c | glow");
    				if (render_status != 0) {
        				fprintf(stderr, "Failed to render markdown (txc/glow issue?). Showing raw text:\n%s\n", ai_output);
   				 }
				} else {
    					fprintf(stderr, "Failed to open markdown.md for writing. Showing raw text:\n%s\n", ai_output);
				}


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
                    fprintf(stderr, "Unexpected API response format.\n");
                 }
            }
            cJSON_Delete(json);
        } else {
            fprintf(stderr, "Failed to parse API response JSON.\n");
        }
    }

    free(postdata);
    free(chunk.response);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    cJSON_Delete(root);
}
int main() {
    openrouter_api_key = getenv("OPENROUTER_API_KEY");
    if(!openrouter_api_key) {
        fprintf(stderr, "Where the fuck is your API key?\n");
        return 1;
    }

    char input[2048];
    char model[128] = "openai/gpt-oss-20b:free";
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
            if (num_selectable_models > 0) {
                printf("Enter model number to use: ");
                char choice_input[16];
                if(fgets(choice_input, sizeof(choice_input), stdin)) {
                    char *endptr;
                    long choice = strtol(choice_input, &endptr, 10);
                    if (endptr != choice_input && (*endptr == '\n' || *endptr == '\0') && choice > 0 && choice <= num_selectable_models) {
                        strncpy(model, selectable_models[choice - 1], sizeof(model) - 1);
                        model[sizeof(model) - 1] = '\0'; 
                        printf("Model set to: %s\n", model);
                    } else {
                        fprintf(stderr, "What the hell? Keeping model: %s\n", model);
                    }
                }
            } else {
                 printf("Ok, someone fuck up and we don't know who\n");
            }
            continue;
        }
        
        add_message("user", input);
        chat_with_openrouter(model, input);
    }
    for(int i = 0; i < history_size; i++) {
        if(history[i].content) free(history[i].content);
    }
    for(int i = 0; i < num_selectable_models; i++) {
        free(selectable_models[i]);
    }
    return 0;
}
