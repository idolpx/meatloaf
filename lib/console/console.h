
#include <stdio.h>
#include <string.h>


enum Mode : uint8_t {
  MODE_LOG = 0x00,
  MODE_SHELL = 0x01,
  MODE_MODEM = 0x02,
  MODE_SLIP = 0x03,
};

class Console {
   private:
    BaseType_t m_task_handle;
    Mode m_mode = MODE_LOG;
    size_t m_baud_rate = DEBUG_SPEED;
    std::string m_prompt = "meatloaf# ";
    std::string m_history_file;

    void repl_task(void *args);

   public:

    // Set Prompt
    void setPrompt(std::string prompt) { this->m_prompt = prompt; }

    // Set Mode
    void setMode(Mode mode) { this->m_mode = mode; }

    void setBaudRate(size_t baud_rate) { this->m_baud_rate = baud_rate; }

    // Start Console Task
    void start();

    void stop();
};