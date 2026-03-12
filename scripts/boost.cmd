@echo off

:: Set Boost version
set BOOST_VERSION=1.81.0

:: Set Boost download URL
set BOOST_URL=https://github.com/boostorg/boost/releases/download/boost-%BOOST_VERSION%/boost-%BOOST_VERSION%.zip

:: Set Boost installation directory
set BOOST_INSTALL_DIR=%CD%\boost-%BOOST_VERSION%

:: Download and extract Boost
powershell -Command "& {Invoke-WebRequest '%BOOST_URL%' -OutFile 'boost.zip'; Expand-Archive -Path 'boost.zip' -DestinationPath '%BOOST_INSTALL_DIR%'}"

:: Run bootstrap
cd %BOOST_INSTALL_DIR%
.\bootstrap.bat

:: Build using b2 & Install Boost
.\b2 --with-system --with-program_options address-model=64 variant=release install --prefix=%BOOST_INSTALL_DIR%
