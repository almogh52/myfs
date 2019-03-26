#include <exception>
#include <string>

class MyFsException: public std::exception {
private:
    std::string message_;
public:
    explicit LoadException(const std::string& message) : message_(message) {

    };
    
    virtual const char* what() const throw() {
        return message_.c_str();
    }
};