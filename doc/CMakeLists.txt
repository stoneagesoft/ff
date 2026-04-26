# Documentation install. Outputs are generated out-of-CMake by
# doc/Makefile (Doxygen + PDFLaTeX); we just copy whatever exists into
# the install tree at install time.

# HTML tree (doxygen output). The `OPTIONAL` keyword keeps `make
# install` from failing when `make doxygen` hasn't been run.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/html/"
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/html
        COMPONENT documentation
        OPTIONAL)

# Reference PDF. The Makefile emits ff-YYYYMMDD.pdf, so we glob at
# install time (configure-time globbing would miss PDFs produced
# after configure but before install — exactly what happens during a
# Debian build). The newest match is shipped as ff-reference.pdf; if
# nothing matches the install step is silently skipped.
install(CODE "
    file(GLOB _ff_pdfs \"${CMAKE_CURRENT_SOURCE_DIR}/ff-*.pdf\")
    if(_ff_pdfs)
        list(SORT _ff_pdfs)
        list(GET _ff_pdfs -1 _ff_pdf_latest)
        file(INSTALL \"\${_ff_pdf_latest}\"
             DESTINATION \"${CMAKE_INSTALL_FULL_DOCDIR}\"
             RENAME ff-reference.pdf)
    endif()
" COMPONENT documentation)
