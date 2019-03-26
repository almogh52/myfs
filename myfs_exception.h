#include <exception>
#include <string>

class MyFsException : public std::exception
{
  private:
    std::string message_;

  public:
    explicit MyFsException(const std::string &message) : message_(message){};
    ~MyFsException() throw() {};

    virtual const char *what() const throw()
    {
        return message_.c_str();
    }
};