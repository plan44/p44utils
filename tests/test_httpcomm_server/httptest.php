<?php


  function generateData($size = false)
  {
    if ($size===false && intval($_REQUEST['size'])>0) {
      $size = $_REQUEST['size'];
    }
    if ($size>0) {
      $data = "";
      for ($line=0; $line<$size/80; $line++) {
        /*      01234567890123456789012345678901234567890123456789012345678901234567890123456789 */
        $data = $data . sprintf("<p>This is line %005d - with a length of 80 characters just to provide data</p>\n", $line);
      }
      echo $data;
    }
  }

  function disable_ob() {
    // Turn off output buffering
    ini_set('output_buffering', 'off');
    // Turn off PHP output compression
    ini_set('zlib.output_compression', false);
    // Implicitly flush the buffer(s)
    ini_set('implicit_flush', true);
    ob_implicit_flush(true);
    // Clear, and turn off output buffering
    while (ob_get_level() > 0) {
        // Get the curent level
        $level = ob_get_level();
        // End the buffering
        ob_end_clean();
        // If the current level has not changed, abort
        if (ob_get_level() == $level) break;
    }
    // Disable apache output buffering/compression
    if (function_exists('apache_setenv')) {
        apache_setenv('no-gzip', '1');
        apache_setenv('dont-vary', '1');
    }
    set_time_limit(0);
  }


  if (intval($_REQUEST['err'])>0) {
    $err = intval($_REQUEST['err']);
    header(sprintf('HTTP/1.1 %d Simulated Error', $err));
    if ($err!=204 && $err!=304) {
      // body only if not 204 or 304
      printf('<html><body><h1>Simulated error %d</h1></body></html>', $err);
    }
  }
  else if (intval($_REQUEST['stream'])>0) {
    disable_ob();
    printf('<html><body><h1>First Chunk: Immediately sent data</h1>' . "\n");
    generateData(1024); // prefix to make sure we can see chunks in browser
    flush();
    ob_flush();
    $numchunks = intval($_REQUEST['stream']);
    $delay = 2;
    if (intval($_REQUEST['delay'])>0) {
      $delay = intval($_REQUEST['delay']);
    }
    for ($n = 0; $n<$numchunks; $n++) {
      sleep($delay);
      printf('<html><body><h1>Delayed Chunk #%d/%d</h1>' . "\n", $n+1, $numchunks);
      generateData();
      flush();
      ob_flush();
    }
  }
  else if (intval($_REQUEST['delay'])>0) {
    disable_ob();
    $delay = intval($_REQUEST['delay']);
    ob_flush();
    flush();
    sleep($delay);
    printf('<html><body><h1>Document delayed by %d seconds OK</h1>', $delay);
    generateData();
    printf('</body></html>');
  }
  else {
    printf('<html><body><h1>Document OK</h1>');
    generateData();
    printf('</body></html>');
  }

?>