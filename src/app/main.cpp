#include "app/application.hpp"

int main(int argc, char** argv) {
    return Application::create()->run(argc, argv);
}
