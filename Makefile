SRCDIR = .
SRCINC = -I.
OBJDIR = .
LIBDIR = 

DB_DIR=./dbc
COMMON_DIR=./common
HANDLERS_DIR=./handlers
CXX=g++-4.8
CFLAGS:= -Wall -g -std=c++11 -Werror -Wno-deprecated -I.. -fPIC -static
LDFLAGS:= -lglog \
                -lsasl2 -lcrypto -lrt -lev -lssl -lpthread -ldl
TARGET:=msg_ctrl

SRC := $(wildcard $(SRCDIR)/*.cpp)
DB_SRC = $(wildcard $(DB_DIR)/*.cpp)
COMMON_SRC = $(wildcard $(COMMON_DIR)/*.cpp)
HANDLERS_SRC = $(wildcard $(HANDLERS_DIR)/*.cpp)

OBJS := $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SRC))
DB_OBJS = $(patsubst $(DB_DIR)/%.cpp, $(OBJDIR)/%.o, $(DB_SRC))
COMMON_OBJS = $(patsubst $(COMMON_DIR)/%.cpp, $(OBJDIR)/%.o, $(COMMON_SRC))
HANDLERS_OBJS = $(patsubst $(HANDLERS_DIR)/%.cpp, $(OBJDIR)/%.o, $(HANDLERS_SRC))

.PHONY : all deps clean veryclean install objs

all : $(TARGET)

objs : $(OBJS) $(DB_OBJS) $(COMMON_OBJS) $(HANDLERS_OBJS)

$(OBJS) : $(OBJDIR)/%.o : $(SRCDIR)/%.cpp
        $(CXX) -c $(CFLAGS) $(SRCINC) $< -o $@

$(DB_OBJS) : $(OBJDIR)/%.o : $(DB_DIR)/%.cpp
        $(CXX) -c $(CFLAGS) $(SRCINC) $< -o $@

$(COMMON_OBJS) : $(OBJDIR)/%.o : $(COMMON_DIR)/%.cpp
        $(CXX) -c $(CFLAGS) $(SRCINC) $< -o $@

$(HANDLERS_OBJS) : $(OBJDIR)/%.o : $(HANDLERS_DIR)/%.cpp
        $(CXX) -c $(CFLAGS) $(SRCINC) $< -o $@

clean:
        rm -f $(OBJDIR)/*.o 
        rm -f *.d
        rm -f $(TARGET)

$(TARGET) : $(OBJS) $(DB_OBJS) $(COMMON_OBJS) $(HANDLERS_OBJS)
        $(CXX) $(OBJS) $(DB_OBJS) $(COMMON_OBJS) $(HANDLERS_OBJS) -o $(TARGET) $(LIBDIR) $(LDFLAGS) 
