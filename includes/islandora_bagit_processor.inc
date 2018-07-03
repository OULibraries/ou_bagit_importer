<?php

/* 
 * This is the general class for handling the bagit for islandora repository
 */

abstract class IslandoraBagitProcessor
{
    static $islandora_content_models = array(
                                        "book"=>"islandora:bookCModel", 
                                        "newspaper"=>"islandora:newspaperCModel", 
                                        "newspaper_issue"=>"islandora:newspaperIssueCModel",
                                        "newspaper_page"=>"islandora:newspaperPageCModel",
                                        "book_page"=>"islandora:pageCModel",
                                        "audio"=>"islandora:sp-audioCModel",
                                        "audio_collection"=>"islandora:collectionCModel",
                                        "pdf"=>"islandora:sp_pdf",
                                        "pdf_collection"=>"islandora:collectionCModel"
                                    );
    // if a model has value true, it means it is a page type content model
    static $sequential_models = array(
        "islandora:newspaperIssueCModel" => false, "islandora:pageCModel" => true, "islandora:newspaperPageCModel" => true, "islandora:sp-audioCModel" => true, "islandora:sp_pdf" => false
    );
    
    var $uri;
    var $parent_collection;
    var $pid_namespace;
    var $recipe_data;
    var $tmp_dir;
    
    function __construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir = NULL)
    {
        $this->uri = $uri;
        $this->parent_collection = $parent_collection;
        $this->pid_namespace = $pid_namespace;
        $this->tmp_dir = $tmp_dir;
        $this->recipe_data = $recipe_data;
    }
    
    function getUri(){
        return $this->uri;
    }
    
    function getParentCollection(){
        return $this->parent_collection;
    }
    
    function getPidNamespace(){
        return $this->pid_namespace;
    }
    
    function getRecipeData(){
        return $this->recipe_data;
    }
    
    function setRecipeData($recipe_data){
        $this->recipe_data = $recipe_data;
    }
    
    function getTmpDir(){
        return $this->tmp_dir;
    }
    
    function setTmpDir($tmp_dir){
        $this->tmp_dir = $tmp_dir;
    }
            
    function prepareImportDataFromBagit(){
        if(empty($this->recipe_data)){
            $file_data = drupal_json_decode(file_get_contents($this->uri));
            if (!$file_data) {
              drupal_set_message(t("Could not download recipe."));
              return;
            }
            $this->setRecipeData($file_data);
        }
        if(empty($this->tmp_dir)){
            $directory_basename = $file_data['recipe']['uuid'];
            $directory = drush_get_option('tmp_dir', "temporary://ou_bagit_importer/{$directory_basename}");
            if (file_prepare_directory($directory, FILE_CREATE_DIRECTORY)) {
                $this->setTmpDir($directory);
            }
        }
    }
    
    function buildObjectModel($object_info) {
        
        $content_types = self::$islandora_content_models; 
        drupal_static_reset('islandora_get_tuque_connection');
        $tuque = islandora_get_tuque_connection();
        $uuid = $this->pid_namespace . ":" . $object_info['uuid'];
        $parent = $object_info['parent_collection'];
        $islandora_object = islandora_object_load($uuid);
        $update = (bool) $object_info['update'];
        $content_model = $content_types[$object_info['import']];

        $new = TRUE;
        if ($islandora_object && !$update) {
          drupal_set_message('Islandora object exists');
          return array(
            'object' => $object_info['uuid'],
            'result' => FALSE,
            'message' => 'Islandora object exists',
          );
        }
        elseif ($islandora_object) {
          $new = FALSE;
        }
        
        if (!$islandora_object) {
            $islandora_object = $tuque->repository->constructObject($uuid);
            $islandora_object->id = $uuid;
            $islandora_object->state = 'A';
            $islandora_object->label = $object_info['label'];
            $islandora_object->ownerId = $user->name;
            $islandora_object->relationships->add(FEDORA_MODEL_URI, 'hasModel', $content_model);
            $islandora_object->relationships->add(FEDORA_RELS_EXT_URI, 'isMemberOf', $parent);            
            if(array_key_exists($content_model, self::$sequential_models) && self::$sequential_models[$content_model] === true){  
                if(!empty($object_info['page_number'])){
                    $page_number = (string)$object_info['page_number'];
                    if(empty($object_info['sequence_number'])){
                        $sequence_number = $page_number;
                    }
                }
                if(!empty($object_info['sequence_number'])){
                    $sequence_number = (string)$object_info['sequence_number'];
                    if(empty($object_info['page_number'])){
                        $page_number = $sequence_number;
                    }
                }
                $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isPageOf', $parent);                    
                $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isPageNumber', $page_number);
                $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', $sequence_number);
                $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSection', '1');
                $this->buildPageObjectDatastreamsFromRecipeData($islandora_object, $object_info);
            }
            else{
                $islandora_object->relationships->add(FEDORA_MODEL_URI, 'hasModel', 'fedora-system:FedoraObject-3.0');
                $this->buildObjectDatastreamsFromRecipeData($islandora_object, $object_info);
                if (array_key_exists($content_model, self::$sequential_models) && self::$sequential_models[$content_model] !== true){
                    $islandora_object->relationships->add(ISLANDORA_RELS_EXT_URI, 'isSequenceNumber', (string)$object_info['sequence_number']);                    
                }
                $this->buildPageObjectDatastreamsFromRecipeData($islandora_object, $object_info);
            }
        }
        else {
          $islandora_object->state = 'A';
          $islandora_object->label = $object_info['label'];
          $islandora_object->ownerId = $user->name;
        }
        
        if ($new) {
            $added_object = islandora_add_object($islandora_object);
            if(!empty($added_object)){
                drupal_set_message(t("The new build object has been sucessfully added. ID: ".$added_object->id));
            }
            else{
                drupal_set_message(t("Could not ingest the new build object "));
                return NULL;
            }
        }

        return $islandora_object;
    }
    
    private function buildPageObjectDatastreamsFromRecipeData(&$islandora_object, $recipe_data){
        $datastreams = array();
        $physical_desc_data = '';
        if (!empty($recipe_data['file'])) {
            try{
            // Check if the OBJ datastream needs to be created.
                if (!empty($islandora_object['OBJ']) && md5($islandora_object['OBJ']->content === $recipe_data['md5'])) {
                    // Do nothing for now.
                } else if ($object_file = $this->copyFileFromRemoteUriToDrupalDirectory($recipe_data['file'], $this->tmp_dir, $recipe_data['md5'])) {
                    // Make sure that object file hash matches hash from recipe.
                    if (strtolower(md5_file($object_file)) !== strtolower($recipe_data['md5'])) {
                        throw new Exception('copied file has mismatched hash: file md5='.md5_file($object_file).' vs recipe md5='.$recipe_data['md5']);
                    }
                    $exif_info = exif_read_data($object_file);
                    if (!empty($exif_info['ImageDescription'])) {
                      $physical_desc_data = htmlspecialchars($exif_info['ImageDescription']);
                    }
                    $finfo = finfo_open(FILEINFO_MIME_TYPE);
                    $datastreams['OBJ'] = array(
                      'type' => finfo_file($finfo, drupal_realpath($object_file)),
                      'content' => file_get_contents($object_file),
                    );
                    finfo_close($finfo);
                }
            }
            catch (Exception $e){
                drupal_set_message(t("Could not download and copy page file @".$recipe_data['file']." with error: ".$e->getMessage()));
                return;
            }
        }
        if (!empty($recipe_data['exif'])) {
            // Create a copy of the exif file since we are dealing with relative links.
            $exif_file = _ou_bagit_importer_copy($recipe_data['source_uri'], $recipe_data['drupal_directory'], $recipe_data['exif']);
            if (file_exists($exif_file)) {
              $datastreams['EXIF'] = array(
                'type' => 'text/plain',
                'content' => file_get_contents($exif_file),
              );
            }
        }
        
        // Capture the MODS, DC data.
        $copyright_data = 'Physical rights are retained by University of Oklahoma. Copyright is retained in accordance with U.S. copyright laws.';
        $mods_record = _ou_bagit_importer_generate_book_page_mods($islandora_object->label, $physical_desc_data, $copyright_data);
        $dc_record = ou_bagit_importer_run_xslt_transform(array(
          'input' => $mods_record,
          'xsl' => drupal_get_path('module', 'islandora_batch') . '/transforms/mods_to_dc.xsl',
        ));
        $datastreams['DC'] = array(
          'type' => 'text/xml',
          'content' => $dc_record,
        );
        $datastreams['MODS'] = array(
          'type' => 'text/xml',
          'content' => $mods_record,
        );

        foreach ($datastreams as $key => $value) {
          if (!$islandora_object[$key]) {
            $ds = $islandora_object->constructDatastream($key);
            $ds->label = $key;
            $ds->mimetype = $value['type'];
            $ds->control_group = 'M';
            $ds->setContentFromString($value['content']);
            $islandora_object->ingestDatastream($ds);
          }
          else {
            $ds = $islandora_object[$key];
            // Only update if the datastream has changed.
            if (md5($ds->content) != md5($value['content'])) {
              $ds->label = $key;
              $ds->mimetype = $value['type'];
              $ds->control_group = 'M';
              $ds->setContentFromString($value['content']);
            }
          }
      }
    }


    private function buildObjectDatastreamsFromRecipeData(&$islandora_object, $recipe_data){
        
        if(!key_exists('metadata', $recipe_data) || empty($recipe_data['metadata'])){
            return;
        }
        
        $mods_record = ou_bagit_importer_run_xslt_transform(array(
            'input' => file_get_contents($recipe_data['metadata']['marcxml']),
            'xsl' => drupal_get_path('module', 'islandora_marcxml') . '/xsl/MARC21slim2MODS3-5.xsl',
        ));
        $dc_record = ou_bagit_importer_run_xslt_transform(array(
            'input' => $mods_record,
            'xsl' => drupal_get_path('module', 'islandora_batch') . '/transforms/mods_to_dc.xsl',
        ));
        $datastreams = array(
            'DC' => array(
              'type' => 'text/xml',
              'content' => $dc_record,
            ),
            'MODS' => array(
              'type' => 'text/xml',
              'content' => $mods_record,
            ),
        );
        if (!empty($recipe_data['metadata']['thumbnail'])) {
            $finfo = finfo_open(FILEINFO_MIME_TYPE);
            $datastreams['OBJ'] = array(
              'type' => finfo_file($finfo, drupal_realpath($recipe_data['metadata']['thumbnail'])),
              'content' => file_get_contents($recipe_data['metadata']['thumbnail']),
            );
            finfo_close($finfo);
        }
        
        foreach ($datastreams as $key => $value) {
            if (!$islandora_object[$key]) {
              $ds = $islandora_object->constructDatastream($key);
              $ds->label = $key;
              $ds->mimetype = $value['type'];
              $ds->control_group = 'M';
              $ds->setContentFromString($value['content']);
              $islandora_object->ingestDatastream($ds);
            }
            else {
                $ds = $islandora_object[$key];
                // Only update this datastream if it has changed.
                if (md5($ds->content) != md5($value['content'])) {
                  $ds->label = $key;
                  $ds->mimetype = $value['type'];
                  $ds->control_group = 'M';
                  $ds->setContentFromString($value['content']);
                }
            }
        }
    }
    
    function updateIslandoraCollectionFromRecipeData($parent_collection, $recipe_data, $default_sequence_number=0) {
        $recipe_data['parent_collection'] = $parent_collection;
        if(empty($recipe_data['sequence_number'])){
            $recipe_data['sequence_number'] = $default_sequence_number;
        }
        return $this->buildObjectModel($recipe_data);
    }
    
    protected function copyFileFromRemoteUriToDrupalDirectory($uri, $dir, $hash = ""){
        if (!ou_bagit_importer_is_path_remote($uri)) {
            return $uri;
        }
        $destination = '';
        try{
            $path = parse_url($uri, PHP_URL_PATH);
            $path_arr = explode("/", $path);
            $file = $path_arr[count($path_arr)-1];
            $destination = "{$dir}/{$file}";
            if (file_exists($destination) && md5_file($destination) == $hash) {
                return $destination;
            }
        }
        catch (Exception $e){
            throw new Exception("Cannot parse the file source information: ".$uri);
        }
        try{
            $status = @copy($uri, $destination);
            if ($status) {
                return $destination;
            }
        }
        catch (Exception $e){
            throw new Exception("Cannot copy file from $uri to $destination");
        }
    }
            
    function outputResults($result, $overwrite = FALSE){
        $directory = $this->tmp_dir;
        $results_file_path = "{$directory}/results.json"; 
        if ($overwrite && file_exists($results_file_path)) {
            $results = (array) drupal_json_decode(file_get_contents($results_file_path));
        }
        $results[$result['object']] = $result;
        file_put_contents($results_file_path, drupal_json_encode($results));
    }
    
    abstract protected function batchImportFromRecipeData();

//    abstract public function buildCollectionObject();
}
