# Makefile, based on makefiletutorial.com

TARGET_EXEC := main

BUILD_DIR := bin
SRC_DIR := src
DEPS_DIR := deps
OBJ_DIR := obj
LIB_DIR := lib

CC := clang
CXX := clang++

# Grab all the C and C++ files we want to compile from the SRC_DIR
# Single quotes around the * lets the wildcard be passed to the find command, not evaluated inline
# The find command returns all the files in a directory, including the directory as a prefix
SRCS := $(shell find $(SRC_DIR) \( -name '*.cpp' -or -name '*.c' \))

DEPS := $(shell find $(DEPS_DIR) \( -name '*.cpp' -or -name '*.c' \))

# Grab the pre-compiled unassembled and unlinked object files
OBJS := $(SRCS:%=$(OBJ_DIR)/%.o) $(DEPS:%=$(OBJ_DIR)/%.o)

# Grab all the .d files correlated to .o
MAKES := $(OBJS:.o=.d)

# All the directories that contain files we need access to at compile?linking? time
INC_DIRS := $(shell find $(SRC_DIR) -type d) $(shell find $(DEPS_DIR) -type d) $(VULKAN_SDK)/include include
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

WARNINGS := all extra no-missing-field-initializers no-unused-parameter
WARNING_FLAGS := $(addprefix -W,$(WARNINGS))

OPTIM_LEVEL := -O0

DEBUG := -g
DEBUG_CPP := _DEBUG

REFERENCE := REFERENCE
RESTIR := NRESTIR

DEFINES := $(REFERENCE) $(RESTIR) $(DEBUG_CPP) KHRONOS_STATIC GLM_ENABLE_EXPERIMENTAL VULKAN_HPP_NO_STRUCT_CONSTRUCTORS IMGUI_IMPL_VULKAN_USE_VOLK
D_FLAGS := $(addprefix -D,$(DEFINES))

# C Preprocessor flags
CPP_FLAGS := $(WARNING_FLAGS) $(INC_FLAGS) -MMD -MP $(OPTIM_LEVEL) $(D_FLAGS) $(DEBUG)

CXX_VERSION := -std=c++20
CXX_FLAGS := $(CXX_VERSION)

C_VERSION := -std=c17
C_FLAGS := $(C_VERSION)

LD_FLAGS := -L$(LIB_DIR) -lglfw3 -lvolk -lktx_read -lslang -lslang-rt

# $@ is target name
# $^ is all prerequisites
# $< is the first prerequisite
# $? is all prerequisites newer than target

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LD_FLAGS)

$(OBJ_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPP_FLAGS) $(C_FLAGS) -c $< -o $@

$(OBJ_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $(CXX_FLAGS) -c $< -o $@

.PHONY: printf clean run

run:
	./$(BUILD_DIR)/$(TARGET_EXEC)

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(BUILD_DIR)

clean_shaders:
	rm -r $(ASSETS_DIR)/$(SPIRVS_DIR)/*.spv

printf:

