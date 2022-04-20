<?php

/**
 * @file
 */

/**
 * Implements hook_drush_command().
 */
function ou_bagit_importer_drush_command() {
  $items = array();

  $items['ou_bagit_importer_import_ou_bag'] = array(
    'callback' => 'drush_ou_bagit_importer_import_ou_bag',
    'aliases' => array('oubib'),
    'description' => 'Import an OU bag from a stream',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_LOGIN,
    'options' => array(
      'recipe_uri' => array(
        'description' => 'Path to recipe file (can be local or remote)',
        'required' => TRUE,
      ),
      'parent_collection' => array(
        'description' => 'Parent collection recipe will be imported into',
        'required' => TRUE,
      ),
      'pid_namespace' => array(
        'description' => 'Namespace to be used for PID creation',
        'required' => TRUE,
      ),
      'tmp_dir' => array(
        'description' => 'Path to tmp downloaded files (will be in public files directory by default)',
        'required' => FALSE,
      ),
    ),
  );

  return $items;
}

/**
 *
 */
function drush_ou_bagit_importer_import_ou_bag() {
  $uri = drush_get_option('recipe_uri');
  $parent_collection = drush_get_option('parent_collection');
  $pid_namespace = drush_get_option('pid_namespace');

  $file_data = drupal_json_decode(file_get_contents($uri));
  if (!$file_data) {
    drupal_set_message(t("Could not download recipe.")); 
  }
  $directory_basename = $file_data['recipe']['uuid'];
  $directory = drush_get_option('tmp_dir', "temporary://ou_bagit_importer/{$directory_basename}");

  $pathinfo = pathinfo($uri);
  $json_filename = $pathinfo['basename'];
  if (file_prepare_directory($directory, FILE_CREATE_DIRECTORY)) {
    drupal_set_message(t("TMP files can be found (and removed manually) at %directory", array('%directory' => $directory)));
    @copy($uri, "{$directory}/{$json_filename}");
    ou_bagit_importer_process_json_file($pathinfo['dirname'], $directory, $json_filename, $parent_collection, $pid_namespace);
  }
  else {
    drupal_set_message(t("Could not create directory. You may need to run drush command as sudo"), 'error');
  }
}

/**
 *
 */
function ou_bagit_importer_process_json_file($source_uri, $drupal_directory, $json_filename, $parent_collection, $pid_namespace) {
  
  $json_data = drupal_json_decode(file_get_contents("{$drupal_directory}/{$json_filename}"));
  if (! $json_data) { 
    drupal_set_message(t('Invalid JSON data!'), 'error');
    return; 
  }
        
  # We only support a limited number of object types.
  # TODO define this list in the module
  $type = $json_data['recipe']['import'];
  if(! in_array( $type,  array( "book", "image", "audio", "pdf", "collection"))) { 
    drupal_set_message( t('Attempt to load unsupported recipe type'), 'error' );
    return;
  }

  # We need local copies of files that potentially live at remote urls 
  # TODO This could use more error handling, probably. 
  $metadata = array();
  if( array_key_exists('metadata', $json_data['recipe'] )) {
    foreach ($json_data['recipe']['metadata'] as $datastream => $path) {
      $metadata[$datastream] = _ou_bagit_importer_copy($source_uri, $drupal_directory, $path);
    }
  }
  $filestreams = array();
  if( array_key_exists('files', $json_data['recipe'] )) {
    foreach ($json_data['recipe']['files'] as $datastream => $path) {
      $filestreams[$datastream] = _ou_bagit_importer_copy($source_uri, $drupal_directory, $path);
    }
  }

  $object = array(
    'pid_namespace' => $pid_namespace,
    'uuid' => $json_data['recipe']['uuid'],
    'label' => $json_data['recipe']['label'],
    'metadata' => $metadata,
    'files' => $filestreams
  );

  if ($type == 'book') {
    # Book is the only compound object that we know how to process
    $object['type'] = 'islandora:bookCModel';
    drupal_set_message(t('Saving @type item @obj', array('@obj' => $object['uuid'],'@type' => $type))); 

    # Process parent object
    $result = ou_bagit_importer_save_item($object, $json_data['recipe'], $parent_collection, $pid_namespace);
    _ou_bagit_importer_write_results("{$drupal_directory}/results.json", $result, TRUE);

    # Queue processing of child pages for book
    ou_bagit_importer_process_json_book_pages($source_uri, $drupal_directory, $object, $json_data['recipe'], $pid_namespace );  
    $batch =& batch_get();
    $batch['progressive'] = FALSE;
    drush_backend_batch_process();

  } elseif ($type == 'collection') {

    # Book is the only compound object that we know how to process
    $object['type'] = 'islandora:collectionCModel';
    drupal_set_message(t('Saving @type item @obj', array('@obj' => $object['uuid'],'@type' => $type))); 

    # Process parent object
    $result = ou_bagit_importer_save_item($object, $json_data['recipe'], $parent_collection, $pid_namespace);
    _ou_bagit_importer_write_results("{$drupal_directory}/results.json", $result, TRUE);

    # Queue processing of member objects
    ou_bagit_importer_process_collection_members($source_uri, $drupal_directory, $object, $json_data['recipe'], $pid_namespace );  
    $batch =& batch_get();
    $batch['progressive'] = FALSE;
    drush_backend_batch_process();

  } else {  

    # Load a simple object
    if ($type == 'audio') {
      $object['type'] = 'islandora:sp-audioCModel';
    } else if ($type == 'image') {
      $object['type'] = 'islandora:sp_large_image_cmodel';
    } else if ($type == 'pdf') {
      $object['type'] = 'islandora:sp_pdf';
    }    
  
    # One step to process a simple object 
    drupal_set_message(t('Saving pdf item @obj', array('@obj' => $object['uuid'],'@type' => $type)));     
    $result = ou_bagit_importer_save_item($object, $json_data['recipe'], $parent_collection, $pid_namespace);
    _ou_bagit_importer_write_results("{$drupal_directory}/results.json", $result, TRUE);
  }
}

/**
 *
 */
function ou_bagit_importer_process_json_book_pages($source_uri, $drupal_directory, $object, $json_data, $pid_namespace) {
  $batch = array(
    'operations' => array(),
    'finished' => '_ou_bagit_importer_batches_finished',
    'title' => dt('Ingesting BAG...'),
    'init_message' => dt('Preparing to ingest BAG...'),
    'progress_message' => dt('Ingesting BAG'),
    'error_message' => dt('File could not get ingested'),
  );
  foreach ($json_data['pages'] as $key => $page) {
    $batch['operations'][] = array(
      'ou_bagit_importer_batch_import_page',
      array(
        $source_uri,
        $drupal_directory,
        $object,
        $key,
        $page,
        $json_data,
        $pid_namespace,
      ),
    );
  }
  batch_set($batch);
}
/**
 *
 */
function ou_bagit_importer_process_collection_members($source_uri, $drupal_directory, $object, $json_data, $pid_namespace) {

}

/**
 *
 */
function ou_bagit_importer_batch_import_page($source_uri, $drupal_directory, $object, $page_number, $page, $json_data, $pid_namespace, &$context ) {
  $result = array();
  $object = array(
    'pid_namespace' => $pid_namespace,
    'source_uri' => $source_uri,
    'drupal_directory' => $drupal_directory,
    'file' => $page['file'],
    'hash' => $page['md5'],
    'exif' => $page['exif'],
    'type' => 'islandora:pageCModel',
    'book' => $json_data['uuid'],
    'page_number' => $page_number,
    'uuid' => $page['uuid'],
    'label' => $page['label'],
    'metadata' => $metadata,
  );
  drupal_set_message(t("Page @page will get ingested here", array('@page' => $file)));
  $result = ou_bagit_importer_save_item($object, $json_data, $object['book'], $pid_namespace);

  if (!empty($result)) {
    _ou_bagit_importer_write_results("{$drupal_directory}/results.json", $result);
  }
}

/**
 *
 */
function ou_bagit_importer_batch_import_item($source_uri, $drupal_directory, $object, $page_number, $page, $json_data, $pid_namespace, &$context ) {
 
}

/**
 *
 */
function _ou_bagit_importer_batches_finished($success, $results, $operations) {
  drush_print('Ingest complete!');
}

/**
 * Checks if the path to bag is remote or local.
 */
function ou_bagit_importer_is_path_remote($path) {
  $host = parse_url($path, PHP_URL_HOST);
  if ($host === NULL) {
    return FALSE;
  }
  else {
    return TRUE;
  }
}

/**
 * Writes result to file.
 */
function _ou_bagit_importer_write_results($filename, $result, $overwrite = FALSE) {
  $results = array();
  if ($overwrite && file_exists($filename)) {
    $results = (array) drupal_json_decode(file_get_contents($filename));
  }
  $results[$result['object']] = $result;
  file_put_contents($filename, drupal_json_encode($results));
}
