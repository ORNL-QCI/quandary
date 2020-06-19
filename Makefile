# Set location of PETSC
#PETSC_DIR = /usr/tce/packages/petsc/petsc-3.12.4-mvapich2-2.3-gcc-4.8-redhat
#PETSC_DIR=/usr/WS2/dubois9/quandry/petsc/
#PETSC_ARCH=arch-linux-c-debug

# Set Braid location 
BRAID_DIR = ${HOME}/Numerics/xbraid_solveadjointwithxbraid
#BRAID_DIR = /usr/workspace/wsb/dubois9/quandry/xbraid

# Set compiler options, e.g. define SANITY_CHECK. Comment out if none.
#CXX_OPT = -DSANITY_CHECK

#######################################################
# Typically no need to change anything below

# Some braid vars
BRAID_INC_DIR = $(BRAID_DIR)/braid
BRAID_LIB_FILE = $(BRAID_DIR)/braid/libbraid.a


# Include some petsc libs, these might change depending on the example you run
include ${PETSC_DIR}/lib/petsc/conf/variables
include ${PETSC_DIR}/lib/petsc/conf/rules
include ${PETSC_DIR}/lib/petsc/conf/test
#
# Set direction of source and header files, and build direction
SRC_DIR   = src
INC_DIR   = include
BUILD_DIR = build

# list all source and object files
SRC_FILES  = $(wildcard $(SRC_DIR)/*.cpp)
SRC_FILES += $(wildcard $(SRC_DIR)/*/*.cpp)
OBJ_FILES  = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRC_FILES))

# set include directory
INC = -I$(INC_DIR) -I$(BRAID_INC_DIR) -I${PETSC_DIR}/include -I${PETSC_DIR}/${PETSC_ARCH}/include 

# Set Library paths and flags
LDPATH  = ${PETSC_DIR}/${PETSC_ARCH}/lib
LDFLAGS = -lpetsc -lm ${BRAID_LIB_FILE} -L${PETSC_DIR}/${PETSC_ARCH}/lib -lblas -llapack

# Set compiler and flags 
CXX=mpicxx
CXXFLAGS= -O3 -std=c++11 -lstdc++ $(CXX_OPT)


# Rule for linking main
main: $(OBJ_FILES)
	$(CXX) -o $@ $(OBJ_FILES) $(LDFLAGS) -L$(LDPATH)

# Rule for building all src files
$(BUILD_DIR)/%.o : $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INC) 
	@$(CXX) -MM $< -MP -MT $@ -MF $(@:.o=.d) $(INC) 


.PHONY: all cleanup

# use 'make cleanup' to remove object files and executable
cleanup:
	rm -fr $(BUILD_DIR) 
	rm -f  main 


# include the dependency files
-include $(OBJ_FILES:.o=.d)
