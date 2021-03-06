/*
 * .: >> general.h
 *
 * ?: Aristoteles Panaras "ale1ster"
 *    Alexandros Mavrogiannis "afein"
 * +: Nikolaos S. Papaspyrou (nickie@softlab.ntua.gr)
 * @: Mon 03 Jun 2013 08:35:52 PM EEST
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <llvm-c/Target.h>
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/Scalar.h>
#include "semantic.h"
#include "error.h"
#include "general.h"
#include "ir.h"
#include "termopts.h"

//Warning implicit declaration of function yyparse during object creation of general.c
extern int yyparse (void);

/* Converts escape sequences of t to the actual characters in s*/
void unescape(char * s, char * t) {
	int i, j;
	i = j = 0;

	while (t[i]) {
		switch (t[i]) {
			case '\\':
				/*  We've found an escape sequence, so translate it  */
				switch( t[++i] ) {
					case 'n':
						s[j] = '\n';
						break;
					case 't':
						s[j] = '\t';
						break;
					case 'r':
						s[j] = '\r';
						break;
					case '0':
						s[j] = '\0';
						break;
					case '\\':
						s[j] = '\\';
						break;
					case '\"':
						s[j] = '\"';
						break;
					case '\'':
						s[j] = '\'';
						break;
					default:
						/*  We don't translate this escape
						    sequence, so just copy it verbatim  */
						s[j++] = '\\';
						s[j] = t[i];
				}
				break;
			default:
				/*  Not an escape sequence, so just copy the character  */
				s[j] = t[i];
		}
		++i;
		++j;
	}
	s[j] = t[i];    /*  Don't forget the null character  */
}

//Cleanup hook.
void cleanup (void) {
	//Removal of tempfile.
	struct stat _;
	if ((our_options.tmp_filename != NULL) && (stat(our_options.tmp_filename, &_) == 0)) {
		if (unlink(our_options.tmp_filename) != 0) {
			my_error(ERR_LV_INTERN, "unlink() call failed");
		}
	}
	if ((our_options.tmp_filename_too != NULL) && (stat(our_options.tmp_filename_too, &_) == 0)) {
		if (unlink(our_options.tmp_filename_too) != 0) {
			my_error(ERR_LV_INTERN, "unlink() call failed");
		}
	}
}

void *new (size_t size) {
	void *result = GC_malloc(size);

	if (result == NULL) {
		my_error(ERR_LV_CRIT, "Out of memory");
	}
	return result;
}

void delete (void *p) {
	if (p != NULL) {
		GC_free(p);
	}
}

//Input filename.
char *filename = "stdin";
extern FILE *yyin;

struct options_t our_options = { .in_file = NULL, .output_type = OUT_NONE, .output_is_stdout = false, .output_filename = NULL, .llvmopt_flags = NULL, .llvmllc_flags = NULL, .llvmclang_flags = NULL, .opt_flag = 0, .pzc_lib_file = NULL };

//LLVM IR dump method.
static void dump_ir (char *outfile) {
	if (outfile != NULL) {
		char *err_msg = NULL;
		if (LLVMPrintModuleToFile(module, outfile, &err_msg)) {
			my_error(ERR_LV_INTERN, "LLVM module not dumped correctly: %s", err_msg);
			LLVMDisposeMessage(err_msg);
		}
	} else {
		LLVMDumpModule(module);
	}
}

/* Turns to false if the IR is unusable */
bool valid_codegen = true;

//Entry point.
int main (int argc, char **argv) {
	pid_t tmp_pid;
	int ret = 0;

	GC_init();

	//Parse command-line options.
	parse_term_options(argc, argv);
	yyin = our_options.in_file;

	module = LLVMModuleCreateWithName("pzcc");
	builder = LLVMCreateBuilder();
	LLVMInitializeNativeTarget();

	initSymbolTable(256);
	openScope();

	generate_external_definitions();	//Declares external function prototypes.

	yyparse();

	closeScope();
	destroySymbolTable();

	if (!valid_codegen) {
		/* Dont optimize or generate final code if the IR is unusable */
		ret = 1;
		goto end;
	}

	//Dump IR to intermediate file.
	dump_ir(our_options.tmp_filename);

	//Optimizer pass on dumped IR.
	if (our_options.opt_flag == true) {
		tmp_pid = fork();
		if (tmp_pid == 0) {
			if (our_options.llvmopt_flags == NULL) {
				if (our_options.verbose_flag == true) {
					fprintf(stderr, "%s %s %s %s %s %s\n", "opt", "-std-compile-opts", "-S", "-o", our_options.tmp_filename, our_options.tmp_filename);
				}
				execlp("opt", "opt", "-std-compile-opts", "-S", "-o", our_options.tmp_filename_too, our_options.tmp_filename, (char *)NULL);
			} else {
				if (our_options.verbose_flag == true) {
					fprintf(stderr, "%s %s %s %s %s %s %s\n", "opt", "-std-compile-opts", our_options.llvmopt_flags, "-S", "-o", our_options.tmp_filename, our_options.tmp_filename);
				}
				execlp("opt", "opt", "-std-compile-opts", our_options.llvmopt_flags, "-S", "-o", our_options.tmp_filename_too, our_options.tmp_filename, (char *)NULL);
			}
			my_error(ERR_LV_INTERN, "'opt' was not found in your path\n");
			exit(EXIT_FAILURE);
		} else if (tmp_pid < 0) {
			my_error(ERR_LV_INTERN, "fork() call failed");
		} else {
			size_t guard = 0;
			int child_status = 0;
			while ((waitpid(tmp_pid, &child_status, 0) != tmp_pid) && (guard < 100)) {
				guard++;
			}
			if (WIFEXITED(child_status)) {
				int exit_status = WEXITSTATUS(child_status);
				if (exit_status != 0) {
					my_error(ERR_LV_INTERN, "execlp() of 'opt' exited with status %d\n", exit_status);
					goto end;
				}
			}
			//Swap filename string pointers, so that the next pass will take the output of the last pass from tmp_filename unconditionally.
			char *_;
			_ = our_options.tmp_filename;
			our_options.tmp_filename = our_options.tmp_filename_too;
			our_options.tmp_filename_too = _;
		}
	}

	switch (our_options.output_type) {
			//If IR was requested, simply copy the temp file to the output file.
		case OUT_IR:
			if (strcmp(our_options.output_filename, "-") != 0) {
				if (rename(our_options.tmp_filename, our_options.output_filename) != 0) {
					tmp_pid = fork();
					if (tmp_pid == 0) {
						execlp("mv", "mv", our_options.tmp_filename, our_options.output_filename, (char *)NULL);
					} else if (tmp_pid < 0) {
						my_error(ERR_LV_INTERN, "fork() call failed");
					} else {
						size_t guard = 0;
						while ((waitpid(tmp_pid, NULL, 0) != tmp_pid) && (guard < 100)) {
							guard++;
						}
					}
				}
			} else {
				tmp_pid = fork();
				if (tmp_pid == 0) {
					execlp("cat", "cat", our_options.tmp_filename, (char *)NULL);
				} else if (tmp_pid < 0) {
					my_error(ERR_LV_INTERN, "fork() call failed");
				} else {
					size_t guard = 0;
					while ((waitpid(tmp_pid, NULL, 0) != tmp_pid) && (guard < 100)) {
						guard++;
					}
				}
			}
			break;
			//If assembly output was requested, generate and dump it to the output file.
		case OUT_ASM:
			tmp_pid = fork();
			if (tmp_pid == 0) {
				switch (our_options.opt_flag) {
					case true	:
						if (our_options.output_filename != NULL) {
							if (our_options.verbose_flag == true) {
								fprintf(stderr, "%s %s %s %s %s %s\n", "llc", "-filetype=asm", "-O3", "-o", our_options.output_filename, our_options.tmp_filename);
							}
							execlp("llc", "llc", "-filetype=asm", "-O3", "-o", our_options.output_filename, our_options.tmp_filename, (char *)NULL);
						} else {
							if (our_options.verbose_flag == true) {
								fprintf(stderr, "%s %s %s %s\n", "llc", "-filetype=asm", "-O3", our_options.tmp_filename);
							}
							execlp("llc", "llc", "-filetype=asm", "-O3", our_options.tmp_filename, (char *)NULL);
						}
						my_error(ERR_LV_INTERN, "'llc' was not found in your path\n");
						exit(EXIT_FAILURE);
						break;
					case false	:
						if (our_options.output_filename != NULL) {
							if (our_options.verbose_flag == true) {
								fprintf(stderr, "%s %s %s %s %s\n", "llc", "-filetype=asm", "-o", our_options.output_filename, our_options.tmp_filename);
							}
							execlp("llc", "llc", "-filetype=asm", "-o", our_options.output_filename, our_options.tmp_filename, (char *)NULL);
						} else {
							if (our_options.verbose_flag == true) {
								fprintf(stderr, "%s %s %s\n", "llc", "-filetype=asm", our_options.tmp_filename);
							}
							execlp("llc", "llc", "-filetype=asm", our_options.tmp_filename, (char *)NULL);
						}
						my_error(ERR_LV_INTERN, "'llc' was not found in your path\n");
						exit(EXIT_FAILURE);
						break;
					default		:
						my_error(ERR_LV_INTERN, "Invalid value in boolean variable");
						break;
				}
			} else if (tmp_pid < 0) {
				my_error(ERR_LV_INTERN, "fork() call failed");
			} else {
				size_t guard = 0;
				int child_status = 0;
				while ((waitpid(tmp_pid, &child_status, 0) != tmp_pid) && (guard < 100)) {
					guard++;
				}
				if (WIFEXITED(child_status)) {
					int exit_status = WEXITSTATUS(child_status);
					if (exit_status != 0) {
						my_error(ERR_LV_INTERN, "execlp() of 'llc' exited with status %d\n", exit_status);
						goto end;
					}
				}
			}
			break;
			//If executable output was requested, the call sequence is llvm-link (IR linking) - llc (IR->obj) - clang (obj -> library obj -> executable).
		case OUT_EXEC:
			//llvm-link call : link with libpzc library in IR level
			tmp_pid = fork();
			if (tmp_pid == 0) {
				if (our_options.verbose_flag == true) {
					fprintf(stderr, "%s %s %s %s %s %s\n", "llvm-link", "-S", "-o", our_options.tmp_filename_too, our_options.tmp_filename, our_options.pzc_lib_file);
				}
				execlp("llvm-link", "llvm-link", "-S", "-o", our_options.tmp_filename_too, our_options.tmp_filename, our_options.pzc_lib_file, (char *)NULL);
				my_error(ERR_LV_INTERN, "'llvm-link' was not found in your path\n");
				exit(EXIT_FAILURE);
			} else if (tmp_pid < 0) {
				my_error(ERR_LV_INTERN, "fork() call failed");
			} else {
				size_t guard = 0;
				int child_status = 0;
				while ((waitpid(tmp_pid, &child_status, 0) != tmp_pid) && (guard < 100)) {
					guard++;
				}
				if (WIFEXITED(child_status)) {
					int exit_status = WEXITSTATUS(child_status);
					if (exit_status != 0) {
						my_error(ERR_LV_INTERN, "execlp() of 'llvm-link' exited with status %d\n", exit_status);
						goto end;
					}
				}
			}
			//llc call : generate object from IR tempfile
			tmp_pid = fork();
			if (tmp_pid == 0) {
				switch (our_options.opt_flag) {
					case true	:
						if (our_options.verbose_flag == true) {
							fprintf(stderr, "%s %s %s %s %s %s\n", "llc", "-filetype=obj", "-O3", "-o", our_options.tmp_filename, our_options.tmp_filename_too);
						}
						execlp("llc", "llc", "-filetype=obj", "-O3", "-o", our_options.tmp_filename, our_options.tmp_filename_too, (char *)NULL);
						break;
					case false	:
						if (our_options.verbose_flag == true) {
							fprintf(stderr, "%s %s %s %s %s\n", "llc", "-filetype=obj", "-o", our_options.tmp_filename, our_options.tmp_filename_too);
						}
						execlp("llc", "llc", "-filetype=obj", "-o", our_options.tmp_filename, our_options.tmp_filename_too, (char *)NULL);
						break;
					default		:
						my_error(ERR_LV_INTERN, "Invalid value in boolean variable");
						break;
				}
			} else if (tmp_pid < 0) {
				my_error(ERR_LV_INTERN, "fork() call failed");
			} else {
				size_t guard = 0;
				int child_status = 0;
				while ((waitpid(tmp_pid, &child_status, 0) != tmp_pid) && (guard < 100)) {
					guard++;
				}
				if (WIFEXITED(child_status)) {
					int exit_status = WEXITSTATUS(child_status);
					if (exit_status != 0) {
						my_error(ERR_LV_INTERN, "execlp() of 'llc' exited with status %d\n", exit_status);
						goto end;
					}
				}
			}
			//clang call : link with library and generate executable output file
			tmp_pid = fork();
			if (tmp_pid == 0) {
				if (our_options.llvmclang_flags != NULL) {
					if (our_options.verbose_flag == true) {
						fprintf(stderr, "%s %s %s %s %s %s\n", "clang", our_options.llvmclang_flags, "-o", our_options.output_filename, our_options.tmp_filename, "-lm");
					}
					execlp("clang", "clang", our_options.llvmclang_flags, "-o", our_options.output_filename, our_options.tmp_filename, "-lm", (char *)NULL);
				} else {
					if (our_options.verbose_flag == true) {
						fprintf(stderr, "%s %s %s %s %s\n", "clang", "-o", our_options.output_filename, our_options.tmp_filename, "-lm");
					}
					execlp("clang", "clang", "-o", our_options.output_filename, our_options.tmp_filename, "-lm", (char *)NULL);
				}
			} else if (tmp_pid < 0) {
				my_error(ERR_LV_INTERN, "fork() call failed");
			} else {
				size_t guard = 0;
				int child_status = 0;
				while ((waitpid(tmp_pid, &child_status, 0) != tmp_pid) && (guard < 100)) {
					guard++;
				}
				if (WIFEXITED(child_status)) {
					int exit_status = WEXITSTATUS(child_status);
					if (exit_status != 0) {
						my_error(ERR_LV_INTERN, "execlp() of 'clang' exited with status %d\n", exit_status);
						goto end;
					}
				}
			}
			break;
		default:
			my_error(ERR_LV_INTERN, "Unknown output file type detected");
	}

end:
	LLVMDisposeBuilder(builder);
	LLVMDisposeModule(module);
	cleanup();

	return ret;
}
