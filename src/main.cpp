#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <iterator>
#include <filesystem>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main(int argc, char *argv[])
{
    if (argc < 3 || std::string(argv[1]) != "-p")
    {
        std::cerr << "Expected first argument to be '-p'" << std::endl;
        return 1;
    }

    std::string prompt = argv[2];

    if (prompt.empty())
    {
        std::cerr << "Prompt must not be empty" << std::endl;
        return 1;
    }

    const char *api_key_env = std::getenv("OPENROUTER_API_KEY");
    const char *base_url_env = std::getenv("OPENROUTER_BASE_URL");

    std::string api_key = api_key_env ? api_key_env : "";
    std::string base_url = base_url_env ? base_url_env : "https://openrouter.ai/api/v1";

    if (api_key.empty())
    {
        std::cerr << "OPENROUTER_API_KEY is not set" << std::endl;
        return 1;
    }

    json messages = json::array({{
        {"role", "user"},
        {"content", prompt},
    }});
    json tools = json::array();

    // Tool 1: Read
    tools.push_back(json::object({{"type", "function"},
                                  {"function", json::object({{"name", "Read"},
                                                             {"description", "Read and return the contents of a file"},
                                                             {"parameters", json::object({{"type", "object"},
                                                                                          {"properties", json::object({{"file_path", json::object({{"type", "string"},
                                                                                                                                                   {"description", "The path to the file to read"}})}})},
                                                                                          {"required", json::array({"file_path"})}})}})}}));

    // Tool 2: Write
    tools.push_back(json::object({{"type", "function"},
                                  {"function", json::object({{"name", "Write"},
                                                             {"description", "Write content to a file"},
                                                             {"parameters", json::object({{"type", "object"},
                                                                                          {"required", json::array({"file_path", "content"})},
                                                                                          {"properties", json::object({{"file_path", json::object({{"type", "string"},
                                                                                                                                                   {"description", "The path of the file to write to"}})},
                                                                                                                       {"content", json::object({{"type", "string"},
                                                                                                                                                 {"description", "The content to write to the file"}})}})}})}})}}));

    // entering the agentic loop

    while (true)
    {
        json request_body = {
            {"model", "anthropic/claude-haiku-4.5"}, // Required model name
            {"messages", messages},
            {"tools", tools}};
        cpr::Response response = cpr::Post(
            cpr::Url{base_url + "/chat/completions"},
            cpr::Header{
                {"Authorization", "Bearer " + api_key},
                {"Content-Type", "application/json"}},
            cpr::Body{request_body.dump()});

        if (response.status_code != 200) // if HTTP request doent succeeded
        {
            std::cerr << "HTTP error: " << response.status_code << "\n"
                      << response.text << std::endl;
            return 1;
        }
        json result = json::parse(response.text); // the server return the respnse and response.text conatain its body as stirng

        if (!result.contains("choices") || result["choices"].empty()) // if result doesnt contian choices or if choices is their but empty
        {
            std::cerr << "No choices in response" << std::endl;
            return 1;
        }
        auto message = result["choices"][0]["message"]; // first choice

        messages.push_back(message);

        if (message.contains("tool_calls") && !message["tool_calls"].empty())
        {
            for (auto &tool_call : message["tool_calls"]) // tool call extracted by the llm first tool
            {                                             //}
                std::string call_id = tool_call["id"];
                std::string function_name = tool_call["function"]["name"];
                std::string tool_output = "";

                std::string argument_str = tool_call["function"]["arguments"];
                json args = json::parse(argument_str);

                if (function_name == "Read")
                {
                    // parsing the arguments JSON string
                    // std::string argument_str = tool_call["function"]["arguments"]; // value of itself is JSON formatted string --"{\"file_path\":\"main.cpp\"}"
                    // json args = json::parse(argument_str);                         // convert the json string to a string object -->{"file_path":"main.cpp"}
                    std::string file_path = args["file_path"]; // using the file path as the key

                    // reading the file from the local file system

                    /**
                     * the llm chooces the pathe
                     *
                     * cpp program receives the path
                     * cpp program performs the file operation
                     */
                    std::ifstream file(file_path);
                    if (!file.is_open())
                    {
                        tool_output = "Error: Failed to open the file" + file_path;
                    }
                    else
                    {
                        tool_output = std::string(
                            (std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
                    }
                }
                else if (function_name == "Write")
                {
                    std::string file_path = args["file_path"];
                    std::string content = args["content"];

                    // ensuring parent directories exist if a nested path provided
                    std::filesystem::path p(file_path);
                    if (p.has_parent_path())
                    {
                        std::filesystem::create_directories(p.parent_path());
                    }
                    // ofstream creates the file if missing or overwrites it by default
                    std::ofstream out_file(file_path, std::ios::out | std::ios::trunc);
                    if (!out_file.is_open())
                    {
                        tool_output = "Error:failed to write to file" + file_path;
                    }
                    else
                    {
                        out_file << content;
                        out_file.close();
                        tool_output = "Successfully wrote to" + file_path;
                    }
                }
                messages.push_back({{"role", "tool"},
                                    {"tool_call_id", call_id},
                                    {"content", tool_output}});
            }
        }
        else
        {
            // no tool call
            if (message.contains("content") && !message["content"].is_null())
            {
                // Fallback to standard mesage content if no tool call was made
                std::cout << message["content"].get<std::string>();
            }
            break;
        }
    }
    return 0;
}

/*json request_body = {
    {"model", "anthropic/claude-haiku-4.5"}, // Required model name
    {"messages", json::array({{{"role", "system"},
                               {"content", "You are a precise assistant. Examine the tools provided in this request, count them, and respond with only the number."}},
                              {{"role", "user"},
                               {"content", prompt}}})},
    {"tools", json::array({{{"type", "function"},
                            {"function", {{"name", "Read"}, {"description", "Read and return the contents of a file"}, {"parameters", {{"type", "object"}, {"properties", {{"file_path", {{"type", "string"}, {"description", "The path to the file to read"}}}}}, {"required", json::array({"file_path"})}}}}}}})}};

cpr::Response response = cpr::Post(
    cpr::Url{base_url + "/chat/completions"},
    cpr::Header{
        {"Authorization", "Bearer " + api_key},
        {"Content-Type", "application/json"}},
    cpr::Body{request_body.dump()});
/**
 *
 *   cpr::Response response = cpr::Post(
cpr::Url{base_url + "/chat/completions"},
cpr::Header{
    {"Authorization", "Bearer " + api_key},
    {"Content-Type", "application/json"}},
cpr::Body{request_body.dump()});
send the request to llm


cpr --> librarty that makes http request
POST--> send the http request to api

Read main.cpp and explain what it does.

ur prorgram creates  ajson request with prompt model and avialbe tools
base_url = "https://openrouter.ai/api/v1"+ "/chat/completions"
api endpoint that recieves ur api request
https://openrouter.ai/api/v1/chat/completions

cpr::Header{ provide information about the request
{"Authorization", "Bearer " + api_key},--> send the api key to server so server can authenticate it
{"Content-Type", "application/json"}--> this tell that request body contains JSON
cpr::Body{request_body.dump()});--> .dump convert normal string inb to json object

200--> succed
400-> invalid reuest
401--> authentication failed
403--> acess forbiden
429--> rate limit issues
500--> rever error


          LLM RESPONSE
           |
           v
  Does it contain
  a tool request?
     /         \
   YES          NO
    |            |
    v            v
Check tool       Check content
name             field
    |            |
    v            v
Execute Read    Print text
    |codecrafters testcodecrafters testcodecrafters test
    v
Print file
contents

 */

/* if (response.status_code != 200) // if HTTP request doent succeeded
 {
     std::cerr << "HTTP error: " << response.status_code << "\n"
               << response.text << std::endl;
     return 1;
 }
 // string string containing json
 json result = json::parse(response.text); // the server return the respnse and response.text conatain its body as stirng

 if (!result.contains("choices") || result["choices"].empty()) // if result doesnt contian choices or if choices is their but empty
 {
     std::cerr << "No choices in response" << std::endl;
     return 1;
 }*/
/*  auto message = result["choices"][0]["message"]; // first choice

  if (message.contains("tool_calls") && !message["tool_calls"].empty())
  {
      auto tool_call = message["tool_calls"][0];                 // tool call extracted by the llm first tool
      std::string function_name = tool_call["function"]["name"]; // extract the name of the requested function
      if (function_name == "Read")
      {
          // parsing the arguments JSON string
          std::string argument_str = tool_call["function"]["arguments"]; // value of itself is JSON formatted string --"{\"file_path\":\"main.cpp\"}"
          json args = json::parse(argument_str);                         // convert the json string to a string object -->{"file_path":"main.cpp"}
          std::string file_path = args["file_path"];                     // using the file path as the key

          // reading the file from the local file system

          /**
           * the llm chooces the pathe
           *
           * cpp program receives the path
           * cpp program performs the file operation
           */
/* std::ifstream file(file_path);
   if (!file.is_open())
   {
       std::cerr << "Failed to open the file " << file_path << std::endl;
       return 1;
   }
   std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
   // read all the characters from the file and store them in a string
   // read the entire file

   // print raw file content directly
   std::cout << content;
}
}
// no tool call
/** else if (message.contains("content") && !message["content"].is_null())
{
// Fallback to standard mesage content if no tool call was made
std::cout << message["content"].get<std::string>();
}

return 0;
}*/

/**
 * {
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": null,
        "tool_calls": [
          {
            "id": "call_abc123",
            "type": "function",
            "function": {
              "name": "Read",
              "arguments": "{\"file_path\": \"/path/to/file.txt\"}"
            }
          }
        ]
      },
      "finish_reason": "tool_calls"
    }
  ]
}
 *
 *
 *
 *
 *
 * {
  "tool_calls": [
    {
      "function": {
        "name": "read"
      }
    },
    {
      "function": {
        "name": "write"
      }
    }
  ]
}





 */