/********************************************************************
* Description: rs274ngc_pre.cc
*
*   Derived from a work by Thomas Kramer
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
* Last change:
* $Revision: 1.49.2.1 $
* $Author: jepler $
* $Date: 2007/11/07 22:14:22 $
********************************************************************/
/* rs274ngc.cc

This rs274ngc.cc file contains the source code for (1) the kernel of
several rs274ngc interpreters and (2) two of the four sets of interface
functions declared in canon.hh:
1. interface functions to call to tell the interpreter what to do.
   These all return a status value.
2. interface functions to call to get information from the interpreter.

Kernel functions call each other. A few kernel functions are called by
interface functions.

Interface function names all begin with "Interp::".

Error handling is by returning a status value of either a non-error
code (INTERP_OK, INTERP_EXIT, etc.) or some specific error code
from each function where there is a possibility of error.  If an error
occurs, processing is always stopped, and control is passed back up
through the function call hierarchy to an interface function; the
error code is also passed back up. The stack of functions called is
also recorded. The external program calling an interface function may
then handle the error further as it sees fit.

Since returned values are usually used as just described to handle the
possibility of errors, an alternative method of passing calculated
values is required. In general, if function A needs a value for
variable V calculated by function B, this is handled by passing a
pointer to V from A to B, and B calculates and sets V.

There are a lot of functions named read_XXXX. All such functions read
characters from a string using a counter. They all reset the counter
to point at the character in the string following the last one used by
the function. The counter is passed around from function to function
by using pointers to it. The first character read by each of these
functions is expected to be a member of some set of characters (often
a specific character), and each function checks the first character.

This version of the interpreter not saving input lines. A list of all
lines will be needed in future versions to implement loops, and
probably for other purposes.

This version does not use any additional memory as it runs. No
memory is allocated by the source code.

This version does not suppress superfluous commands, such as a command
to start the spindle when the spindle is already turning, or a command
to turn on flood coolant, when flood coolant is already on.  When the
interpreter is being used for direct control of the machining center,
suppressing superfluous commands might confuse the user and could be
dangerous, but when it is used to translate from one file to another,
suppression can produce more concise output. Future versions might
include an option for suppressing superfluous commands.

****************************************************************************/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <libintl.h>

#include "inifile.hh"		// INIFILE
#include "rs274ngc.hh"
#include "rs274ngc_return.hh"
#include "interp_internal.hh"	// interpreter private definitions
//#include "rs274ngc_errors.cc"

#include "units.h"

extern char * _rs274ngc_errors[];

#undef LOG_FILE

#define LOG_FILE &_setup.log_file[0]

void Interp::doLog(char *fmt, ...)
{
#ifdef LOG_FILE
    struct timeval tv;
    struct tm *tm;
    va_list ap;

    va_start(ap, fmt);

    if(log_file == NULL)
    {
       log_file = fopen(LOG_FILE, "a");
    }

    if(log_file == NULL)
    {
         fprintf(stderr, "(%d)Unable to open log file:%s\n",
                  getpid(), LOG_FILE);
    }

    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);

    fprintf(log_file, "%04d%02d%02d-%02d:%02d:%02d.%03ld ",
	    tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	    tm->tm_hour, tm->tm_min, tm->tm_sec,
	    tv.tv_usec/1000);

    vfprintf(log_file, fmt, ap);
    fflush(log_file);

    va_end(ap);
#endif
}

/****************************************************************************/

/*

The functions in this section of this file are functions for
external programs to call to tell the rs274ngc interpreter
what to do. They are declared in rs274ngc.hh.

*/

/***********************************************************************/

/*! Interp::close

Returned Value: int (INTERP_OK)

Side Effects:
   The NC-code file is closed if open.
   The _setup world model is reset.

Called By: external programs

*/

int Interp::close()
{
  if (_setup.file_pointer != NULL) {
    fclose(_setup.file_pointer);
    _setup.file_pointer = NULL;
    _setup.percent_flag = OFF;
  }
  reset();

  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::execute

Returned Value: int)
   If execute_block returns INTERP_EXIT, this returns that.
   If execute_block returns INTERP_EXECUTE_FINISH, this returns that.
   If execute_block returns an error code, this returns that code.
   Otherwise, this returns INTERP_OK.

Side Effects:
   Calls to canonical machining commands are made.
   The interpreter variables are changed.
   At the end of the program, the file is closed.
   If using a file, the active G codes and M codes are updated.

Called By: external programs

This executes a previously parsed block.

*/

int Interp::execute(const char *command)
{
  static char name[] = "Interp::execute";
  int status;
  int n;

  if (NULL != command) {
    status = read(command);
    if (status != INTERP_OK) {
      return status;
    }
  }

  logDebug("Interp::execute(%s)", command);
  // process control functions -- will skip if skipping
  if (_setup.block1.o_number != 0)
    {
      CHP(convert_control_functions(&(_setup.block1), &_setup));
#if 0
//!!!KL -- this is test code that is wrong
// an attempt to let MDI code call subroutines.      
    
      logDebug("!!!KL Open file is:%s:", _setup.filename);
      while(_setup.file_pointer) // we have an open file, execute it to completion
      {
          status = read(0);  // reads from current file and calls parse
          if (status != INTERP_OK) {
               return status;
          }
          execute();
      }
//!!!KL -- end of test code
#endif
        
      return INTERP_OK;
    }

  // skip if skipping
  if(_setup.skipping_o)
    {
      logDebug("skipping to line: %d", _setup.skipping_o);
      return INTERP_OK;
    }

  for (n = 0; n < _setup.parameter_occurrence; n++)
  {  // copy parameter settings from parameter buffer into parameter table
    _setup.parameters[_setup.parameter_numbers[n]]
      = _setup.parameter_values[n];
  }

  logDebug("_setup.named_parameter_occurrence = %d",
           _setup.named_parameter_occurrence);
  for (n = 0; n < _setup.named_parameter_occurrence; n++)
  {  // copy parameter settings from parameter buffer into parameter table

      logDebug("storing param");
      logDebug("storing param:|%s|", _setup.named_parameters[n]);
    CHP(store_named_param(_setup.named_parameters[n],
                          _setup.named_parameter_values[n]));

    // free the string
      logDebug("freeing param[%d]:|%s|:0x%x", n, _setup.named_parameters[n],
               _setup.named_parameters[n]);
    free(_setup.named_parameters[n]);
  }

  _setup.named_parameter_occurrence = 0;

  if (_setup.line_length != 0) {        /* line not blank */
    status = execute_block(&(_setup.block1), &_setup);
    write_g_codes(&(_setup.block1), &_setup);
    write_m_codes(&(_setup.block1), &_setup);
    write_settings(&_setup);
    if ((status != INTERP_OK) &&
        (status != INTERP_EXECUTE_FINISH) && (status != INTERP_EXIT))
      ERP(status);
  } else                        /* blank line is OK */
    status = INTERP_OK;
  return status;
}

/***********************************************************************/

/*! Interp::exit

Returned Value: int (INTERP_OK)

Side Effects: See below

Called By: external programs

The system parameters are saved to a file and some parts of the world
model are reset. If GET_EXTERNAL_PARAMETER_FILE_NAME provides a
non-empty file name, that name is used for the file that is
written. Otherwise, the default parameter file name is used.

*/

int Interp::exit()
{
  char file_name[LINELEN];

  GET_EXTERNAL_PARAMETER_FILE_NAME(file_name, (LINELEN - 1));
  save_parameters(((file_name[0] ==
                             0) ?
                            RS274NGC_PARAMETER_FILE_NAME_DEFAULT :
                            file_name), _setup.parameters);
  reset();

  return INTERP_OK;
}

/***********************************************************************/

/*! rs274_ngc_init

Returned Value: int
   If any of the following errors occur, this returns the error code shown.
   Otherwise, this returns INTERP_OK.
   1. Interp::restore_parameters returns an error code.

Side Effects:
   Many values in the _setup structure are reset.
   A USE_LENGTH_UNITS canonical command call is made.
   A SET_FEED_REFERENCE canonical command call is made.
   A SET_ORIGIN_OFFSETS canonical command call is made.
   An INIT_CANON call is made.

Called By: external programs

Currently we are running only in CANON_XYZ feed_reference mode.  There
is no command regarding feed_reference in the rs274 language (we
should try to get one added). The initialization routine, therefore,
always calls SET_FEED_REFERENCE(CANON_XYZ).

*/

int Interp::init()
{
  static char name[] = "Interp::init";
  int k;                        // starting index in parameters of origin offsets
  int status;
  char filename[LINELEN];
  double *pars;                 // short name for _setup.parameters

  char *iniFileName;

  INIT_CANON();

  iniFileName = getenv("INI_FILE_NAME");

  // the default log file
  strcpy(&_setup.log_file[0], "emc_log");
  _setup.loggingLevel = 0;

  // not clear -- but this is fn is called a second time without an INI.
  if(NULL == iniFileName)
  {
      logDebug("INI_FILE_NAME not found");
  }
  else
  {
      IniFile inifile;

      logDebug("iniFileName:%s:", iniFileName);

      if (inifile.Open(iniFileName) == false) {
          logDebug("Unable to open inifile:%s:", iniFileName);
      }
      else
      {
          const char *inistring;


          if(NULL != (inistring = inifile.Find("LOG_LEVEL", "RS274NGC")))
          {
              _setup.loggingLevel = atol(inistring);
          }

          if(NULL != (inistring = inifile.Find("LOG_FILE", "RS274NGC")))
          {
	    // found it
            realpath(inistring, &_setup.log_file[0]);
          }

          if(NULL != (inistring = inifile.Find("PROGRAM_PREFIX", "DISPLAY")))
          {
	    // found it
            realpath(inistring, _setup.program_prefix);
            logDebug("program prefix:%s: prefix:%s:", inistring, _setup.program_prefix);
          }
          else
          {
	      logDebug("PROGRAM_PREFIX not found");
          }


          // close it
          inifile.Close();
      }
  }

  _setup.length_units = GET_EXTERNAL_LENGTH_UNIT_TYPE();
  USE_LENGTH_UNITS(_setup.length_units);
  GET_EXTERNAL_PARAMETER_FILE_NAME(filename, LINELEN);
  if (filename[0] == 0)
    strcpy(filename, RS274NGC_PARAMETER_FILE_NAME_DEFAULT);
  CHP(restore_parameters(filename));
  pars = _setup.parameters;
  _setup.origin_index = (int) (pars[5220] + 0.0001);
  if(_setup.origin_index < 1 || _setup.origin_index > 9) {
      _setup.origin_index = 1;
      pars[5220] = 1.0;
  }

  k = (5200 + (_setup.origin_index * 20));
  SET_ORIGIN_OFFSETS(USER_TO_PROGRAM_LEN(pars[k + 1] + pars[5211]),
                     USER_TO_PROGRAM_LEN(pars[k + 2] + pars[5212]), 
                     USER_TO_PROGRAM_LEN(pars[k + 3] + pars[5213]),
                     USER_TO_PROGRAM_ANG(pars[k + 4] + pars[5214]),
                     USER_TO_PROGRAM_ANG(pars[k + 5] + pars[5215]),
                     USER_TO_PROGRAM_ANG(pars[k + 6] + pars[5216]),
                     USER_TO_PROGRAM_LEN(pars[k + 7] + pars[5217]),
                     USER_TO_PROGRAM_LEN(pars[k + 8] + pars[5218]),
                     USER_TO_PROGRAM_LEN(pars[k + 9] + pars[5219]));
  SET_FEED_REFERENCE(CANON_XYZ);
  _setup.AA_axis_offset = USER_TO_PROGRAM_ANG(pars[5214]);
//_setup.Aa_current set in Interp::synch
  _setup.AA_origin_offset = USER_TO_PROGRAM_ANG(pars[k + 4]);
  _setup.BB_axis_offset = USER_TO_PROGRAM_ANG(pars[5215]);
//_setup.Bb_current set in Interp::synch
  _setup.BB_origin_offset = USER_TO_PROGRAM_ANG(pars[k + 5]);
  _setup.CC_axis_offset = USER_TO_PROGRAM_ANG(pars[5216]);
//_setup.Cc_current set in Interp::synch
  _setup.CC_origin_offset = USER_TO_PROGRAM_ANG(pars[k + 6]);
  _setup.u_axis_offset = USER_TO_PROGRAM_LEN(pars[5217]);
  _setup.u_origin_offset = USER_TO_PROGRAM_LEN(pars[k + 7]);
  _setup.v_axis_offset = USER_TO_PROGRAM_LEN(pars[5218]);
  _setup.v_origin_offset = USER_TO_PROGRAM_LEN(pars[k + 8]);
  _setup.w_axis_offset = USER_TO_PROGRAM_LEN(pars[5219]);
  _setup.w_origin_offset = USER_TO_PROGRAM_LEN(pars[k + 9]);
//_setup.active_g_codes initialized below
//_setup.active_m_codes initialized below
//_setup.active_settings initialized below
  _setup.axis_offset_x = USER_TO_PROGRAM_LEN(pars[5211]);
  _setup.axis_offset_y = USER_TO_PROGRAM_LEN(pars[5212]);
  _setup.axis_offset_z = USER_TO_PROGRAM_LEN(pars[5213]);
//_setup.block1 does not need initialization
  _setup.blocktext[0] = 0;
//_setup.current_slot set in Interp::synch
//_setup.current_x set in Interp::synch
//_setup.current_y set in Interp::synch
//_setup.current_z set in Interp::synch
  _setup.cutter_comp_side = OFF;
//_setup.cycle values do not need initialization
  _setup.distance_mode = MODE_ABSOLUTE;
  _setup.feed_mode = UNITS_PER_MINUTE;
//_setup.feed_override set in Interp::synch
//_setup.feed_rate set in Interp::synch
  _setup.filename[0] = 0;
  _setup.file_pointer = NULL;
//_setup.flood set in Interp::synch
  _setup.tool_offset_index = 1;
//_setup.length_units set in Interp::synch
  _setup.line_length = 0;
  _setup.linetext[0] = 0;
//_setup.mist set in Interp::synch
  _setup.motion_mode = G_80;
//_setup.origin_index set above
  _setup.origin_offset_x = USER_TO_PROGRAM_LEN(pars[k + 1]);
  _setup.origin_offset_y = USER_TO_PROGRAM_LEN(pars[k + 2]);
  _setup.origin_offset_z = USER_TO_PROGRAM_LEN(pars[k + 3]);
//_setup.parameters set above
//_setup.parameter_occurrence does not need initialization
//_setup.parameter_numbers does not need initialization
//_setup.parameter_values does not need initialization
//_setup.percent_flag does not need initialization
//_setup.plane set in Interp::synch
  _setup.probe_flag = OFF;
  _setup.toolchange_flag = OFF;
  _setup.input_flag = OFF;
  _setup.input_index = -1;
  _setup.input_digital = OFF;
  _setup.program_x = 0.;   /* for cutter comp */
  _setup.program_y = 0.;   /* for cutter comp */
  _setup.program_z = 0.;   /* for cutter comp */
  _setup.cutter_comp_firstmove = ON;
//_setup.retract_mode does not need initialization
//_setup.selected_tool_slot set in Interp::synch
  _setup.sequence_number = 0;   /*DOES THIS NEED TO BE AT TOP? */
//_setup.speed set in Interp::synch
  _setup.speed_feed_mode = CANON_INDEPENDENT;
//_setup.speed_override set in Interp::synch
//_setup.spindle_turning set in Interp::synch
//_setup.stack does not need initialization
//_setup.stack_index does not need initialization
  _setup.tool_xoffset = 0.0;
  _setup.tool_zoffset = 0.0;
//_setup.tool_max set in Interp::synch
//_setup.tool_table set in Interp::synch
//_setup.traverse_rate set in Interp::synch
//_setup.adaptive_feed set in Interp::synch
//_setup.feed_hold set in Interp::synch

  // initialization stuff for subroutines and control structures
  _setup.call_level = 0;
  _setup.defining_sub = 0;
  _setup.skipping_o = 0;
  _setup.oword_labels = 0;

  memcpy(_readers, default_readers, sizeof(default_readers));

  long axis_mask = GET_EXTERNAL_AXIS_MASK();
  if(!(axis_mask & AXIS_MASK_X)) _readers[(int)'x'] = 0;
  if(!(axis_mask & AXIS_MASK_Y)) _readers[(int)'y'] = 0;
  if(!(axis_mask & AXIS_MASK_Z)) _readers[(int)'z'] = 0;
  if(!(axis_mask & AXIS_MASK_A)) _readers[(int)'a'] = 0;
  if(!(axis_mask & AXIS_MASK_B)) _readers[(int)'b'] = 0;
  if(!(axis_mask & AXIS_MASK_C)) _readers[(int)'c'] = 0;
  if(!(axis_mask & AXIS_MASK_U)) _readers[(int)'u'] = 0;
  if(!(axis_mask & AXIS_MASK_V)) _readers[(int)'v'] = 0;
  if(!(axis_mask & AXIS_MASK_W)) _readers[(int)'w'] = 0;

  synch(); //synch first, then update the interface


  write_g_codes((block_pointer) NULL, &_setup);
  write_m_codes((block_pointer) NULL, &_setup);
  write_settings(&_setup);


  // Synch rest of settings to external world
  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::load_tool_table

Returned Value: int
   If any of the following errors occur, this returns the error code shown.
   Otherwise, this returns INTERP_OK.
   1. _setup.tool_max is larger than CANON_TOOL_MAX: NCE_TOOL_MAX_TOO_LARGE

Side Effects:
   _setup.tool_table[] is modified.

Called By:
   Interp::synch
   external programs

This function calls the canonical interface function GET_EXTERNAL_TOOL_TABLE
to load the whole tool table into the _setup.

The CANON_TOOL_MAX is an upper limit for this software. The
_setup.tool_max is intended to be set for a particular machine.

*/

int Interp::load_tool_table()
{
  static char name[] = "Interp::load_tool_table";
  int n;

  CHK((_setup.tool_max > CANON_TOOL_MAX), NCE_TOOL_MAX_TOO_LARGE);
  for (n = 0; n <= _setup.tool_max; n++) {
    _setup.tool_table[n] = GET_EXTERNAL_TOOL_TABLE(n);
  }
  for (; n <= CANON_TOOL_MAX; n++) {
    _setup.tool_table[n].id = 0;
    _setup.tool_table[n].xoffset = 0;
    _setup.tool_table[n].zoffset = 0;
    _setup.tool_table[n].diameter = 0;
    _setup.tool_table[n].orientation = 0;
    _setup.tool_table[n].frontangle = 0;
    _setup.tool_table[n].backangle = 0;
  }

  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::open

Returned Value: int
   If any of the following errors occur, this returns the error code shown.
   Otherwise it returns INTERP_OK.
   1. A file is already open: NCE_A_FILE_IS_ALREADY_OPEN
   2. The name of the file is too long: NCE_FILE_NAME_TOO_LONG
   3. The file cannot be opened: NCE_UNABLE_TO_OPEN_FILE

Side Effects: See below

Called By: external programs

The file is opened for reading and _setup.file_pointer is set.
The file name is copied into _setup.filename.
The _setup.sequence_number, is set to zero.
Interp::reset() is called, changing several more _setup attributes.

The manual [NCMS, page 3] discusses the use of the "%" character at the
beginning of a "tape". It is not clear whether it is intended that
every NC-code file should begin with that character.

In the following, "uses percents" means the first non-blank line
of the file must consist of nothing but the percent sign, with optional
leading and trailing white space, and there must be a second line
of the same sort later on in the file. If a file uses percents,
execution stops at the second percent line. Any lines after the
second percent line are ignored.

In this interpreter (recalling that M2 and M30 always ends execution):
1. If execution of a file is ended by M2 or M30 (not necessarily on
the last line of the file), then it is optional that the file
uses percents.
2. If execution of a file is not ended by M2 or M30, then it is
required that the file uses percents.

If the file being opened uses percents, this function turns on the
_setup.percent flag, reads any initial blank lines, and reads the
first line with the "%". If not, after reading enough to determine
that, this function puts the file pointer back at the beginning of the
file.

*/

int Interp::open(const char *filename) //!< string: the name of the input NC-program file
{
  static char name[] = "Interp::open";
  char *line;
  int index;
  int length;

  CHK((_setup.file_pointer != NULL), NCE_A_FILE_IS_ALREADY_OPEN);
  CHK((strlen(filename) > (LINELEN - 1)), NCE_FILE_NAME_TOO_LONG);
  _setup.file_pointer = fopen(filename, "r");
  CHK((_setup.file_pointer == NULL), NCE_UNABLE_TO_OPEN_FILE);
  line = _setup.linetext;
  for (index = -1; index == -1;) {      /* skip blank lines */
    CHK((fgets(line, LINELEN, _setup.file_pointer) ==
         NULL), NCE_FILE_ENDED_WITH_NO_PERCENT_SIGN);
    length = strlen(line);
    if (length == (LINELEN - 1)) {   // line is too long. need to finish reading the line to recover
      for (; fgetc(_setup.file_pointer) != '\n';);      // could look for EOF
      ERM(NCE_COMMAND_TOO_LONG);
    }
    for (index = (length - 1);  // index set on last char
         (index >= 0) && (isspace(line[index])); index--);
  }
  if (line[index] == '%') {
    for (index--; (index >= 0) && (isspace(line[index])); index--);
    if (index == -1) {
      _setup.percent_flag = ON;
      _setup.sequence_number = 1;       // We have already read the first line
      // and we are not going back to it.
    } else {
      fseek(_setup.file_pointer, 0, SEEK_SET);
      _setup.percent_flag = OFF;
      _setup.sequence_number = 0;       // Going back to line 0
    }
  } else {
    fseek(_setup.file_pointer, 0, SEEK_SET);
    _setup.percent_flag = OFF;
    _setup.sequence_number = 0; // Going back to line 0
  }
  strcpy(_setup.filename, filename);
  reset();
  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::read

Returned Value: int
   If any of the following errors occur, this returns the error code shown.
   Otherwise, this returns:
       a. INTERP_ENDFILE if the only non-white character on the line is %,
       b. INTERP_EXECUTE_FINISH if the first character of the
          close_and_downcased line is a slash, and
       c. INTERP_OK otherwise.
   1. The command and_setup.file_pointer are both NULL: INTERP_FILE_NOT_OPEN
   2. The probe_flag is ON but the HME command queue is not empty:
      NCE_QUEUE_IS_NOT_EMPTY_AFTER_PROBING
   3. If read_text (which gets a line of NC code from file) or parse_line
     (which parses the line) returns an error code, this returns that code.

Side Effects:
   _setup.sequence_number is incremented.
   The _setup.block1 is filled with data.

Called By: external programs

This reads a line of NC-code from the command string or, (if the
command string is NULL) from the currently open file. The
_setup.line_length will be set by read_text. This will be zero if the
line is blank or consists of nothing but a slash. If the length is not
zero, this parses the line into the _setup.block1.

*/

int Interp::read(const char *command)  //!< may be NULL or a string to read
{
  static char name[] = "Interp::read";
  int status;
  int read_status;

  if (_setup.probe_flag == ON) {
    CHK((GET_EXTERNAL_QUEUE_EMPTY() == 0),
        NCE_QUEUE_IS_NOT_EMPTY_AFTER_PROBING);
    set_probe_data(&_setup);
    _setup.probe_flag = OFF;
  }
  if (_setup.toolchange_flag == ON) {
    CHKF((GET_EXTERNAL_QUEUE_EMPTY() == 0),
         (_("Queue is not empty after tool change")));
    refresh_actual_position(&_setup);
    _setup.toolchange_flag = OFF;
  }
  if (_setup.input_flag == ON) {
    CHK((GET_EXTERNAL_QUEUE_EMPTY() == 0),
        NCE_QUEUE_IS_NOT_EMPTY_AFTER_INPUT);
    if (_setup.input_digital == ON) { // we are checking for a digital input
	_setup.parameters[5399] = GET_EXTERNAL_DIGITAL_INPUT(_setup.input_index);
    } else { // checking for analog input
	_setup.parameters[5399] = GET_EXTERNAL_ANALOG_INPUT(_setup.input_index);
    }
    _setup.input_flag = OFF;
  }
  CHK(((command == NULL) && (_setup.file_pointer == NULL)),
      INTERP_FILE_NOT_OPEN);

  if(_setup.file_pointer)
  {
     _setup.block1.offset = ftell(_setup.file_pointer);
  }

  read_status =
    read_text(command, _setup.file_pointer, _setup.linetext,
              _setup.blocktext, &_setup.line_length);

  if(command)logDebug("%s:[cmd]:|%s|", name, command);
  else logDebug("%s:|%s|", name, _setup.linetext);

  if ((read_status == INTERP_EXECUTE_FINISH)
      || (read_status == INTERP_OK)) {
    if (_setup.line_length != 0) {
      CHP(parse_line(_setup.blocktext, &(_setup.block1), &_setup));
    }

    else // Blank line (zero length)
    {
          /* RUM - this case reached when the block delete '/' character 
             is used, or READ_FULL_COMMENT is OFF and a comment is the
             only content of a line. 
             If a block o-type is in effect, block->o_number needs to be 
             incremented to allow o-extensions to work. 
             Note that the the block is 'refreshed' by init_block(),
             not created new, so this is a legal operation on block1. */
        if (_setup.block1.o_type != O_none)
        {
            // Clear o_type, this isn't line isn't a command...
            _setup.block1.o_type = 0;
            // increment o_number
            _setup.block1.o_number++;
        }
    }
  } else if (read_status == INTERP_ENDFILE);
  else
    ERP(read_status);
  return read_status;
}

/***********************************************************************/

/*! Interp::reset

Returned Value: int (INTERP_OK)

Side Effects: See below

Called By:
   external programs
   Interp::close
   Interp::exit
   Interp::open

This function resets the parts of the _setup model having to do with
reading and interpreting one line. It does not affect the parts of the
model dealing with a file being open; Interp::open and Interp::close
do that.

There is a hierarchy of resetting the interpreter. Each of the following
calls does everything the ones above it do.

Interp::reset()
Interp::close()
Interp::init()

In addition, Interp::synch and Interp::restore_parameters (both of
which are called by Interp::init) change the model.

*/

int Interp::reset()
{
  _setup.linetext[0] = 0;
  _setup.blocktext[0] = 0;
  _setup.line_length = 0;

  // initialization stuff for subroutines and control structures
  _setup.call_level = 0;
  _setup.defining_sub = 0;
  _setup.skipping_o = 0;
  _setup.oword_labels = 0;

  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::restore_parameters

Returned Value:
  If any of the following errors occur, this returns the error code shown.
  Otherwise it returns INTERP_OK.
  1. The parameter file cannot be opened for reading: NCE_UNABLE_TO_OPEN_FILE
  2. A parameter index is out of range: NCE_PARAMETER_NUMBER_OUT_OF_RANGE
  3. The parameter file is not in increasing order:
     NCE_PARAMETER_FILE_OUT_OF_ORDER

Side Effects: See below

Called By:
  external programs
  Interp::init

This function restores the parameters from a file, modifying the
parameters array. Usually parameters is _setup.parameters. The file
contains lines of the form:

<variable number> <value>

e.g.

5161 10.456

The variable numbers must be in increasing order, and certain
parameters must be included, as given in the _required_parameters
array. These are the axis offsets, the origin index (5220), and nine
sets of origin offsets. Any parameter not given a value in the file
has its value set to zero.

*/
int Interp::restore_parameters(const char *filename)   //!< name of parameter file to read  
{
  static char name[] = "Interp::restore_parameters";
  FILE *infile;
  char line[256];
  int variable;
  double value;
  int required;                 // number of next required parameter
  int index;                    // index into _required_parameters
  double *pars;                 // short name for _setup.parameters
  int k;

  // open original for reading
  infile = fopen(filename, "r");
  CHK((infile == NULL), NCE_UNABLE_TO_OPEN_FILE);

  pars = _setup.parameters;
  k = 0;
  index = 0;
  required = _required_parameters[index++];
  while (feof(infile) == 0) {
    if (fgets(line, 256, infile) == NULL) {
      break;
    }
    // try for a variable-value match in the file
    if (sscanf(line, "%d %lf", &variable, &value) == 2) {
      CHK(((variable <= 0)
           || (variable >= RS274NGC_MAX_PARAMETERS)),
          NCE_PARAMETER_NUMBER_OUT_OF_RANGE);
      for (; k < RS274NGC_MAX_PARAMETERS; k++) {
        if (k > variable)
          ERM(NCE_PARAMETER_FILE_OUT_OF_ORDER);
        else if (k == variable) {
          pars[k] = value;
          if (k == required)
            required = _required_parameters[index++];
          k++;
          break;
        } else                  // if (k < variable)
        {
          if (k == required)
            required = _required_parameters[index++];
          pars[k] = 0;
        }
      }
    }
  }
  fclose(infile);
  for (; k < RS274NGC_MAX_PARAMETERS; k++) {
    pars[k] = 0;
  }
  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::save_parameters

Returned Value:
  If any of the following errors occur, this returns the error code shown.
  Otherwise it returns INTERP_OK.
  1. The existing file cannot be renamed:  NCE_CANNOT_CREATE_BACKUP_FILE
  2. The renamed file cannot be opened to read: NCE_CANNOT_OPEN_BACKUP_FILE
  3. The new file cannot be opened to write: NCE_CANNOT_OPEN_VARIABLE_FILE
  4. A parameter index is out of range: NCE_PARAMETER_NUMBER_OUT_OF_RANGE
  5. The renamed file is out of order: NCE_PARAMETER_FILE_OUT_OF_ORDER

Side Effects: See below

Called By:
   external programs
   Interp::exit

A file containing variable-value assignments is updated. The old
version of the file is saved under a different name.  For each
variable-value pair in the old file, a line is written in the new file
giving the current value of the variable.  File lines have the form:

<variable number> <value>

e.g.

5161 10.456

If a required parameter is missing from the input file, this does not
complain, but does write it in the output file.

*/
int Interp::save_parameters(const char *filename,      //!< name of file to write
                             const double parameters[]) //!< parameters to save   
{
  static char name[] = "Interp::save_parameters";
  FILE *infile;
  FILE *outfile;
  char line[256];
  int variable;
  double value;
  int required;                 // number of next required parameter
  int index;                    // index into _required_parameters
  int k;

  // rename as .bak
  strcpy(line, filename);
  strcat(line, RS274NGC_PARAMETER_FILE_BACKUP_SUFFIX);
  CHK((rename(filename, line) != 0), NCE_CANNOT_CREATE_BACKUP_FILE);

  // open backup for reading
  infile = fopen(line, "r");
  CHK((infile == NULL), NCE_CANNOT_OPEN_BACKUP_FILE);

  // open original for writing
  outfile = fopen(filename, "w");
  CHK((outfile == NULL), NCE_CANNOT_OPEN_VARIABLE_FILE);

  k = 0;
  index = 0;
  required = _required_parameters[index++];
  while (feof(infile) == 0) {
    if (fgets(line, 256, infile) == NULL) {
      break;
    }
    // try for a variable-value match
    if (sscanf(line, "%d %lf", &variable, &value) == 2) {
      CHK(((variable <= 0)
           || (variable >= RS274NGC_MAX_PARAMETERS)),
          NCE_PARAMETER_NUMBER_OUT_OF_RANGE);
      for (; k < RS274NGC_MAX_PARAMETERS; k++) {
        if (k > variable)
          ERM(NCE_PARAMETER_FILE_OUT_OF_ORDER);
        else if (k == variable) {
          sprintf(line, "%d\t%f\n", k, parameters[k]);
          fputs(line, outfile);
          if (k == required)
            required = _required_parameters[index++];
          k++;
          break;
        } else if (k == required)       // know (k < variable)
        {
          sprintf(line, "%d\t%f\n", k, parameters[k]);
          fputs(line, outfile);
          required = _required_parameters[index++];
        }
      }
    }
  }
  fclose(infile);
  for (; k < RS274NGC_MAX_PARAMETERS; k++) {
    if (k == required) {
      sprintf(line, "%d\t%f\n", k, parameters[k]);
      fputs(line, outfile);
      required = _required_parameters[index++];
    }
  }
  fclose(outfile);
  return INTERP_OK;
}

/***********************************************************************/

/*! Interp::synch

Returned Value: int (INTERP_OK)

Side Effects:
   sets the value of many attribute of _setup by calling various
   GET_EXTERNAL_xxx functions.

Called By:
   Interp::init
   external programs

This function gets the _setup world model in synch with the rest of
the controller.

*/

int Interp::synch()
{

  char file_name[LINELEN];

  _setup.control_mode = GET_EXTERNAL_MOTION_CONTROL_MODE();
  _setup.AA_current = GET_EXTERNAL_POSITION_A();
  _setup.BB_current = GET_EXTERNAL_POSITION_B();
  _setup.CC_current = GET_EXTERNAL_POSITION_C();
  _setup.current_slot = GET_EXTERNAL_TOOL_SLOT();
  _setup.current_x = GET_EXTERNAL_POSITION_X();
  _setup.current_y = GET_EXTERNAL_POSITION_Y();
  _setup.current_z = GET_EXTERNAL_POSITION_Z();
  _setup.u_current = GET_EXTERNAL_POSITION_U();
  _setup.v_current = GET_EXTERNAL_POSITION_V();
  _setup.w_current = GET_EXTERNAL_POSITION_W();
  _setup.feed_rate = GET_EXTERNAL_FEED_RATE();
  _setup.flood = (GET_EXTERNAL_FLOOD() != 0) ? ON : OFF;
  _setup.length_units = GET_EXTERNAL_LENGTH_UNIT_TYPE();
  _setup.mist = (GET_EXTERNAL_MIST() != 0) ? ON : OFF;
  _setup.plane = GET_EXTERNAL_PLANE();
  _setup.selected_tool_slot = GET_EXTERNAL_SELECTED_TOOL_SLOT();
  _setup.speed = GET_EXTERNAL_SPEED();
  _setup.spindle_turning = GET_EXTERNAL_SPINDLE();
  _setup.tool_max = GET_EXTERNAL_TOOL_MAX();
  _setup.traverse_rate = GET_EXTERNAL_TRAVERSE_RATE();
  _setup.feed_override = GET_EXTERNAL_FEED_OVERRIDE_ENABLE();
  _setup.speed_override = GET_EXTERNAL_SPINDLE_OVERRIDE_ENABLE();
  _setup.adaptive_feed = GET_EXTERNAL_ADAPTIVE_FEED_ENABLE();
  _setup.feed_hold = GET_EXTERNAL_FEED_HOLD_ENABLE();

  GET_EXTERNAL_PARAMETER_FILE_NAME(file_name, (LINELEN - 1));
  save_parameters(((file_name[0] ==
                             0) ?
                            RS274NGC_PARAMETER_FILE_NAME_DEFAULT :
                            file_name), _setup.parameters);

  load_tool_table();   /*  must set  _setup.tool_max first */

  return INTERP_OK;
}

/***********************************************************************/
/***********************************************************************/

/*

The functions in this section are to extract information from the
interpreter.

*/

/***********************************************************************/

/*! Interp::active_g_codes

Returned Value: none

Side Effects: copies active G codes into the codes array

Called By: external programs

See documentation of write_g_codes.

*/

void Interp::active_g_codes(int *codes)        //!< array of codes to copy into
{
  int n;

  for (n = 0; n < ACTIVE_G_CODES; n++) {
    codes[n] = _setup.active_g_codes[n];
  }
}

/***********************************************************************/

/*! Interp::active_m_codes

Returned Value: none

Side Effects: copies active M codes into the codes array

Called By: external programs

See documentation of write_m_codes.

*/

void Interp::active_m_codes(int *codes)        //!< array of codes to copy into
{
  int n;

  for (n = 0; n < ACTIVE_M_CODES; n++) {
    codes[n] = _setup.active_m_codes[n];
  }
}

/***********************************************************************/

/*! Interp::active_settings

Returned Value: none

Side Effects: copies active F, S settings into array

Called By: external programs

See documentation of write_settings.

*/

void Interp::active_settings(double *settings) //!< array of settings to copy into
{
  int n;

  for (n = 0; n < ACTIVE_SETTINGS; n++) {
    settings[n] = _setup.active_settings[n];
  }
}

static char savedError[LINELEN+1];
void Interp::setError(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);

    vsnprintf(savedError, LINELEN, fmt, ap);

    va_end(ap);
}

/***********************************************************************/

/*! Interp::error_text

Returned Value: none

Side Effects: see below

Called By: external programs

This copies the error string whose index in the _rs274ngc_errors array
is error_code into the error_text array -- unless the error_code is
an out-of-bounds index or the length of the error string is not less
than max_size, in which case an empty string is put into the
error_text. The length of the error_text array should be at least
max_size.

*/

void Interp::error_text(int error_code,        //!< code number of error                
                         char *error_text,      //!< char array to copy error text into  
                         int max_size)  //!< maximum number of characters to copy
{
    if(error_code == NCE_VARIABLE)
    {
        strncpy(error_text, savedError, max_size);
        error_text[max_size-1] = 0;

        return;
    }

    if ((error_code >= INTERP_MIN_ERROR) && (error_code <= RS274NGC_MAX_ERROR))
    {
    	char *message = _(_rs274ngc_errors[error_code]);
	strncpy(error_text, message, max_size-1);
	error_text[max_size-1] = 0;
    }
    else
    {
        error_text[0] = 0;
    }
}

/***********************************************************************/

/*! Interp::file_name

Returned Value: none

Side Effects: see below

Called By: external programs

This copies the _setup.filename (the name of the currently open
file) into the file_name array -- unless the name is not shorter than
max_size, in which case a null string is put in the file_name array.


*/

void Interp::file_name(char *file_name,        //!< string: to copy file name into      
                        int max_size)   //!< maximum number of characters to copy
{
  if (strlen(_setup.filename) < ((size_t) max_size))
    strcpy(file_name, _setup.filename);
  else
    file_name[0] = 0;
}

/***********************************************************************/

/*! Interp::line_length

Returned Value: the length of the most recently read line

Side Effects: none

Called By: external programs

*/

int Interp::line_length()
{
  return _setup.line_length;
}

/***********************************************************************/

/*! Interp::line_text

Returned Value: none

Side Effects: See below

Called By: external programs

This copies at most (max_size - 1) non-null characters of the most
recently read line into the line_text string and puts a NULL after the
last non-null character.

*/

void Interp::line_text(char *line_text,        //!< string: to copy line into           
                        int max_size)   //!< maximum number of characters to copy
{
  int n;
  char *the_text;

  the_text = _setup.linetext;
  for (n = 0; n < (max_size - 1); n++) {
    if (the_text[n] != 0)
      line_text[n] = the_text[n];
    else
      break;
  }
  line_text[n] = 0;
}

/***********************************************************************/

/*! Interp::sequence_number

Returned Value: the current interpreter sequence number (how many
lines read since the last time the sequence number was reset to zero,
which happens only when Interp::init or Interp::open is called).

Side Effects: none

Called By: external programs

*/

int Interp::sequence_number()
{
  return _setup.sequence_number;
}

/***********************************************************************/

/*! Interp::stack_name

Returned Value: none

Side Effects: see below

Called By: external programs

This copies at most (max_size - 1) non-null characters from the
string whose index in the _setup.stack array is stack_index into the
function_name string and puts a NULL after the last non-null character --
unless the stack_index is an out-of-bounds index, in which case an
empty string is put into the function_name.

This function is intended to be used several times in a row to get the
stack of function calls that existed when the most recent error
occurred. It should be called first with stack_index equal to 0,
next with stack_index equal to 1, and so on, stopping when an
empty string is returned for the name.

*/

void Interp::stack_name(int stack_index,       //!< index into stack of function names  
                         char *function_name,   //!< string: to copy function name into
                         int max_size)  //!< maximum number of characters to copy
{
  int n;
  char *the_name;

  if ((stack_index > -1) && (stack_index < 20)) {
    the_name = _setup.stack[stack_index];
    for (n = 0; n < (max_size - 1); n++) {
      if (the_name[n] != 0)
        function_name[n] = the_name[n];
      else
        break;
    }
    function_name[n] = 0;
  } else
    function_name[0] = 0;
}

/***********************************************************************/

/* Interp::ini_load()

Returned Value: INTERP_OK, RS274NGC_ERROR

Side Effects:
   An INI file containing values for global variables is used to
   update the globals

Called By:
   Interp::init()

The file looks like this:

[RS274NGC]
VARIABLE_FILE = rs274ngc.var

*/

int Interp::ini_load(const char *filename)
{
    IniFile inifile;
    const char *inistring;

    // open it
    if (inifile.Open(filename) == false) {
        logDebug("Unable to open inifile:%s:", filename);
	return -1;
    }

    logDebug("Opened inifile:%s:", filename);

#if 1
    if (NULL != (inistring = inifile.Find("PARAMETER_FILE", "RS274NGC"))) {
	// found it
	strncpy(_parameter_file_name, inistring, LINELEN);
        logDebug("found PARAMETER_FILE:%s:", _parameter_file_name);
    } else {
	// not found, leave RS274NGC_PARAMETER_FILE alone
        logDebug("did not find PARAMETER_FILE");
    }
#endif

#if 0    
    if (NULL != (inistring = inifile.Find("PROGRAM_PREFIX", "DISPLAY"))) {
	// found it
        realpath(inistring, _setup.program_prefix);
        logDebug("inistring:%s: prefix:%s:", inistring, _setup.program_prefix);
    } else {
	// not found
        logDebug("inistring not found");
    }
#endif


    // close it
    inifile.Close();

    return 0;
}
