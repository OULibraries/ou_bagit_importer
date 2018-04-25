<?php

/* 
 * This class is specific for newspaper module
 */

class IslandoraBagitAudioProcessor extends IslandoraBagitProcessor {
    
    function __construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir)
    {
        parent::__construct($uri, $parent_collection, $pid_namespace, $recipe_data, $tmp_dir);
    }
    
    function batchImportFromRecipeData(){
        $this->prepareImportDataFromBagit();
        $recipe_data = $this->getRecipeData();
        $audio_collection = $this->updateIslandoraCollectionFromRecipeData($this->getParentCollection(), $recipe_data['recipe']);
        if(empty($audio_collection)){
            drupal_set_message("Cannot add new audio collection ".($recipe_data['recipe']['label']));
            throw new Exception("Cannot add new audio collection ".($recipe_data['recipe']['label']));
        }
        $audios = $recipe_data['recipe']['audios'];
        
        $batch = array(
            'operations' => array(),
            'finished' => '_ou_bagit_importer_batches_finished',
            'title' => dt('Ingesting BAG...'),
            'init_message' => dt('Preparing to ingest BAG...'),
            'progress_message' => dt('Ingesting BAG'),
            'error_message' => dt('Page could not get ingested'),
        );
        
        foreach ($audios as $audio_number => $audio_data){
            $batch['operations'][] = array(
                'ou_bagit_importer_batch_import_page_by_processor',
                array(serialize($this), $audio_collection->id, $audio_data, $audio_number+1),
            );
        }
        
        batch_set($batch);
        drupal_set_message(t('Page processing has been done.'));
    }
    
}
