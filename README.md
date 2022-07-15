rtsp-auth-test
==============

This is a simple self-contained application for testing RTSP authentication support in libcurl, which has been [broken](https://curl.se/docs/knownbugs.html#RTSP_authentication_breaks_witho) for a while. It spawns a fake RTSP server that requires authentication, and then uses cURL to try and send an authenticated RTSP DESCRIBE to the local server. The main aim of this simple application is providing with a quick and easy way to replicate the RTSP authentication problem in a completely self-contained way.

Assuming the right dependencies are installed, just type

	make

to compile, and

	./rtsp-auth-test

to run the test application.

Since the DESCRIBE is sent using a libcurl easy session in verbose mode, you'll be able to see if the server returns `200 OK` (authentication worked as expected) or `401 Unauthorized` (authentication failed). In my local tests on Fedora 35, authentication works when using libcurl 7.78.0, and fails when using 7.79.1. Notice that on versions later than 7.66 it only works thanks to an [ugly workaround](https://github.com/curl/curl/pull/4750): RTSP authentication was broken in 7.66 already, and probably versions later than 7.78.0 broke the workaround as well.
