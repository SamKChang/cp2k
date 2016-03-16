! Basic use statements and preprocessor macros
! should be included in the use statements

  USE base_hooks,                      ONLY: cp__a,&
                                             cp__b,&
                                             cp__w,&
                                             cp__l,&
                                             cp_abort,&
                                             cp_warn,&
                                             timeset,&
                                             timestop


! Dangerous: Full path can be arbitrarily long and might overflow Fortran line.
#if !defined(__SHORT_FILE__)
#define __SHORT_FILE__ __FILE__
#endif

#define __LOCATION__ cp__l(__SHORT_FILE__,__LINE__)
#define CPWARN(msg) CALL cp__w(__SHORT_FILE__,__LINE__,msg)
#define CPABORT(msg) CALL cp__b(__SHORT_FILE__,__LINE__,msg)
#define CPASSERT(cond) IF(.NOT.(cond))CALL cp__a(__SHORT_FILE__,__LINE__)

! The MARK_USED macro can be used to mark an argument/variable as used.
! It is intended to make it possible to switch on -Werror=unused-dummy-argument,
! but deal elegantly with e.g. library wrapper routines that take arguments only used if the library is linked in. 
! This code should be valid for any Fortran variable, is always standard conforming,
! and will be optimized away completely by the compiler
#define MARK_USED(foo) IF(.FALSE.)THEN; DO ; IF(SIZE(SHAPE(foo))==-1) EXIT ;  END DO ; ENDIF

! Macro which is calculating a single version number out of three components (major, minor, update).
! This is helpful when writing preprocessor conditions for code depending on the compiler version.
#define CP_VERSION3(MAJOR, MINOR, UPDATE) (MAJOR * 10000 + MINOR * 100 + UPDATE)

! The CP_CONTIGUOUS macro may (or may not) expand to the CONTIGUOUS attribute
! depending on whether or not the compiler supports Fortran 2008.
#if defined(__GFORTRAN__) && (40600 <= CP_FVERSION3(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__))
# define CP_CONTIGUOUS CONTIGUOUS
#elif defined(__INTEL_COMPILER) && (1210 <= __INTEL_COMPILER)
# define CP_CONTIGUOUS CONTIGUOUS
#else
# define CP_CONTIGUOUS
#endif
! Represents the comma eventually needed to attach the CONTIGUOUS attribute.
! Not used directly but eventually expanded by CP_COMMA(CP_CONTIGUOUS).
#define CP_COMMA_CONTIGUOUS ,

! Concatenate two symbols.
#define CP_CONCATENATE_AUX(A, B) A##B
! Concatenate two symbols but expanding the arguments first.
#define CP_CONCATENATE(A, B) CP_CONCATENATE_AUX(A, B)

! Expands to a comma eventually needed when using ATTRIB depending on
! whether or not the ATTRIB is defined, otherwise expanded to nothing.
#define CP_COMMA(ATTRIB) CP_CONCATENATE(CP_COMMA_, ATTRIB)

