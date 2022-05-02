<?php

/**
 * @file
 * Batch, json processing, and cli interface
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
 * Process json recipe file and dispatch import functions.
 */
function ou_bagit_importer_process_json_file($source_uri, $drupal_directory, $json_filename, $parent_collection, $pid_namespace) {
  
  $json_data = drupal_json_decode(file_get_contents("{$drupal_directory}/{$json_filename}"));
  if (! $json_data) { 
    drupal_set_message(t('Invalid JSON data!'), 'error');
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
    'update' => $json_data['recipe']['update'],
    'import' => $json_data['recipe']['import'],
    'metadata' => $metadata,
    'files' => $filestreams,
  );
 
  # One step to process a main object 
  drupal_set_message(t('Saving item @obj', array('@obj' => $object['uuid'] )));
  $result = ou_bagit_importer_save_item($object, $parent_collection, $pid_namespace);
  _ou_bagit_importer_write_results("{$drupal_directory}/results.json", $result, TRUE);

  # Queue processing of member objects if present
  # Book pages and member objects are treated similarly
  if (array_key_exists('members', $json_data['recipe']) || array_key_exists('pages', $json_data['recipe'])  ) {
    ou_bagit_importer_process_collection($source_uri, $drupal_directory, $object, $json_data['recipe'], $pid_namespace);
    $batch = &batch_get();
    $batch['progressive'] = FALSE;
    drush_backend_batch_process();
  }  
}

/**
 * Queue up batch load of member or page objects.
 */
function ou_bagit_importer_process_collection($source_uri, $drupal_directory, $object, $json_data, $pid_namespace) {
  $batch = array(
    'operations' => array(),
    'finished' => '_ou_bagit_importer_batches_finished',
    'title' => dt('Ingesting BAG...'),
    'init_message' => dt('Preparing to ingest BAG...'),
    'progress_message' => dt('Ingesting BAG'),
    'error_message' => dt('File could not get ingested'),
  );

  // Recipes should include members or pages, and shouldn't have both. We'll check for pages, becasue the book 
  // format is older, and then we'll check for members.
  // TODO merge this in to one case 
  if (!empty($json_data['pages'])) {
    foreach ($json_data['pages'] as $key => $page) {
      $batch['operations'][] = array(
        'ou_bagit_importer_batch_import_item',
        array(
          $source_uri,
          $drupal_directory,
          $key,
          $page,
          $json_data,
          $pid_namespace,
        ),
      );
    }
  } else if (!empty($json_data['members'])) {
    foreach ($json_data['members'] as $key => $page) {
      $batch['operations'][] = array(
        'ou_bagit_importer_batch_import_item',
        array(
          $source_uri,
          $drupal_directory,
          $key,
          $page,
          $json_data,
          $pid_namespace,
        ),
      );
    }
  }

  // Queue up the import work.
  batch_set($batch);
}


/**
 * Do a batched item import. 
 */
function ou_bagit_importer_batch_import_item($source_uri, $drupal_directory, $page_number, $item, $json_data, $pid_namespace, &$context ) {
  
  $result = array();

  $object = array(
    'pid_namespace' => $pid_namespace,
    'source_uri' => $source_uri,
    'drupal_directory' => $drupal_directory,
    'file' => $item['file'],
    'files'=> $item['files'],
    'hash' => $item['md5'],
    'exif' => $item['exif'],
    'import' => $item['import'],
    'update' => $json_data['recipe']['update'],
    'page_number' => $page_number,
    'uuid' => $item['uuid'],
    'label' => $item['label'],
    'metadata' => $item['metadata'],
  );
  drupal_set_message(t("Item @item will be ingested.", array('@item' => $item['label'])));
  $result = ou_bagit_importer_save_item($object, $pid_namespace.':'.$json_data['uuid'], $pid_namespace);

  if (!empty($result)) {
    _ou_bagit_importer_write_results("{$drupal_directory}/results.json", $result);
  }
}

/**
 * Check if the path to bag is remote or local.
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
 * Handle batch finished event
 */
function _ou_bagit_importer_batches_finished($success, $results, $operations) {
  drush_print('Ingest complete!');
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
